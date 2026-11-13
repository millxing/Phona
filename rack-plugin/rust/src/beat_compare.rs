//! Offline diagnostic only. Private wrapper access stays out of the shipped library.
use super::*;
use chimera::control::beat_player::BeatPlayer;
use chimera::voice::human_voice::HumanVoice;
use std::{io::Write, path::Path};
const FS: usize = 48000;

fn save(dir: &Path, name: &str, x: &[f32]) {
    assert!(x.iter().all(|v| v.is_finite()), "{name}: nonfinite");
    assert!(x.iter().any(|v| v.abs()>0.001), "{name}: silent");
    let mut f=std::fs::File::create(dir.join(format!("{name}.f32"))).unwrap();
    for v in x { f.write_all(&v.to_le_bytes()).unwrap(); }
}

#[test]
#[ignore = "offline diagnostic; requires PHONA_COMPARE_DIR"]
fn render_comparison() {
    let dir=std::path::PathBuf::from(std::env::var("PHONA_COMPARE_DIR").expect("PHONA_COMPARE_DIR"));
    std::fs::create_dir_all(&dir).unwrap();
    for (female,body_name) in [(0.0,"male"),(1.0,"female")] {
        let body=BodyMix::new(female,0.0);
        for (case,tokens,spacing) in [
            ("B",vec![1],0.5),("Pf",vec![2],0.5),("t",vec![3],0.5),
            ("pattern",vec![1,3,2,3,1,3,2,3],0.25),
            ("fast",vec![1,3,2,3,1,3,2,3],0.125)] {
            for variant in ["wrapper","folds","limiter","no_cancel","native_live","native_planned"] {
                let total=((0.5 + tokens.len() as f64*spacing+0.7)*FS as f64) as usize;
                let mut out=vec![0f32;total];
                let ids=["","B","Pf","t"];
                if variant=="native_planned" {
                    // Native CLI recipe: future gestures, enabled source, default limiter.
                    let mut v=HumanVoice::new(FS as f64,body.body(),2);
                    v.set_articulation(&beat_ready(body),None);
                    v.set_source(&SourcePatch { f0:Some(100.0),voiced:Some(true),pressure:Some(8.0),..Default::default() });
                    v.set_gate(0.0);
                    let mut player=BeatPlayer::new(); player.body=body;
                    let mut gs=Vec::new();
                    for (i,tok) in tokens.iter().enumerate() {
                        player.gestures(&BeatStep::new(ids[*tok as usize]),0.5+i as f64*spacing,0.18,&mut gs);
                    }
                    v.schedule(&gs);
                    v.render(&mut out);
                } else if variant=="native_live" {
                    let mut e=Engine::new(FS as f64);e.set_body(body);
                    e.set_artic(&beat_ready(body),None);
                    e.set_source(&SourcePatch { f0:Some(100.0),voiced:Some(true),pressure:Some(8.0),..Default::default() });
                    e.set_gate(0.0);
                    for (k,s) in out.iter_mut().enumerate() {
                        let start=FS/2+47; let step=(spacing*FS as f64) as usize;
                        if k>=start && (k-start)%step==0 {
                            if let Some(tok)=tokens.get((k-start)/step) { e.beat_hit(&BeatStep::new(ids[*tok as usize])); }
                        }
                        e.render(std::slice::from_mut(s));
                    }
                } else {
                    let ptr=chim_new(FS as f64,female,0.0);assert!(!ptr.is_null());
                    chim_set_beatbox(ptr,1);
                    let v=unsafe{&mut *ptr};
                    if variant=="folds" {
                        v.engine.voice_mut().fold_scale=1.0;
                        v.engine.set_source(&SourcePatch::from_source(&DEFAULT_SOURCE));
                    }
                    if variant=="limiter" { v.engine.voice_mut().limiter=true; }
                    for (k,s) in out.iter_mut().enumerate() {
                        let start=FS/2+47;let step=(spacing*FS as f64) as usize;
                        if k>=start && (k-start)%step==0 {
                            if let Some(tok)=tokens.get((k-start)/step) {
                                if variant=="no_cancel" {
                                    let v=unsafe{&mut *ptr};
                                    v.engine.beat_hit(&BeatStep::new(ids[*tok as usize]));v.beat_muted=false;
                                } else { chim_beat_hit(ptr,*tok); }
                            }
                        }
                        chim_render(ptr,s,1);
                    }
                    assert_eq!(chim_is_dead(ptr),0);chim_free(ptr);
                }
                save(&dir,&format!("{body_name}_{case}_{variant}"),&out);
            }
        }
    }
}

#[test]
#[ignore = "offline hat experiment; requires PHONA_COMPARE_DIR"]
fn render_hat_pulse() {
    use chimera::control::score::ControlKey;
    let dir=std::path::PathBuf::from(std::env::var("PHONA_COMPARE_DIR").expect("PHONA_COMPARE_DIR"));
    for (name,scale) in [("baseline",1.0),("half",0.5),("quarter",0.25)] {
        let ptr=chim_new(FS as f64,0.0,0.0);
        assert!(!ptr.is_null());
        chim_set_beatbox(ptr,1);
        let mut out=vec![0.0;FS*2];
        for (k,s) in out.iter_mut().enumerate() {
            if k==FS/2+47 {
                let v=unsafe{&mut *ptr};
                v.engine.stop_speech(); v.engine.set_gate(0.0);
                let mut gs=Vec::new();
                let lead=BeatPlayer::lead_in_with("t",BodyMix::MALE,&mut gs);
                gs.clear();
                BeatPlayer::new().gestures(&BeatStep::new("t"),0.0,0.18,&mut gs);
                for g in &mut gs {
                    if g.targets.has(ControlKey::BREATH) {
                        g.t1=g.t0+(g.t1-g.t0)*scale;
                        g.ramp_out*=scale;
                    }
                }
                v.engine.schedule(&gs,lead,false); v.beat_muted=false;
            }
            chim_render(ptr,s,1);
        }
        assert_eq!(chim_is_dead(ptr),0);chim_free(ptr);
        save(&dir,&format!("hat_pulse_{name}"),&out);
    }
}
