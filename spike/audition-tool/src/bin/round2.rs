use chimera::anatomy::human::{ArticKey, ArticPatch, BodyMix};
use chimera::control::score::ControlKey;
use chimera::phonetics::{Segment, Syllable};
use chimera::phonetics::planner::vowel_artic;
use chimera::phonetics::sing::{plan_song, midi_to_hz, SongOptions, SungNote};
use chimera::voice::human_voice::{HumanVoice, SourcePatch};
use std::{fs, path::Path};
const FS: f64 = 48000.;
fn main() {
    chimera::init();
    let args: Vec<String> = std::env::args().collect();
    let specs: serde_json::Value = serde_json::from_str(&fs::read_to_string(&args[1]).unwrap()).unwrap();
    let dir = Path::new(&args[2]); fs::create_dir_all(dir).unwrap();
    for s in specs.as_array().unwrap() {
        let name = s["name"].as_str().unwrap();
        let body = BodyMix::new(s["female"].as_f64().unwrap(), s["child"].as_f64().unwrap());
        let source: SourcePatch = serde_json::from_value(s["source"].clone()).unwrap();
        let mut v = HumanVoice::new(FS, body.body(), 2);
        v.set_source(&source);
        let lar = s["larynx_delta_cm"].as_f64().unwrap();
        let duration;
        if s["kind"] == "phrase" {
            let base = s["base_midi"].as_f64().unwrap();
            let notes: Vec<SungNote> = [0.,2.,4.,7.,0.].iter().enumerate().map(|(i,d)| SungNote {
                midi: base+d, beats: if i==4 {2.} else {1.},
                syl: Syllable { text:"ah".into(), onset:vec![], nucleus:Segment::new("ɑ"), coda:vec![], melisma:None, word_end:true, phrase_end:i==4 }
            }).collect();
            let mut plan = plan_song(&notes, &SongOptions {body, bpm:110., t0:0.3, vibrato:0.});
            // Raise each calibrated vowel target, rather than letting score gestures override a base-only change.
            if lar != 0. {
                for g in &mut plan.gestures {
                    if let Some(x) = g.targets.get(ArticKey::Larynx) { g.targets.set(ArticKey::Larynx,x+lar); }
                }
                let mut a = ArticPatch::default(); a.set(ArticKey::Larynx, vowel_artic("ɑ",body).unwrap().get(ArticKey::Larynx).unwrap_or(0.)+lar);
                v.set_articulation(&a,Some(true));
            }
            // No planner vibrato: depth is controlled solely by the source for this audition.
            for g in &mut plan.gestures { g.targets.remove(ControlKey::VIBRATO); }
            v.schedule(&plan.gestures);
            duration = 0.3+6.*60./110.+0.4;
            // Match the source posture to the first score note (default source otherwise).
            v.set_source(&SourcePatch::f0(midi_to_hz(base)));
        } else {
            v.set_articulation(&vowel_artic("ɑ",body).unwrap(),Some(true));
            duration=3.5;
        }
        let mut out=vec![0f32; (duration*FS).round() as usize];
        // 48-frame blocks put 0.100, 0.700 and 3.100 s events on exact sample boundaries.
        for (i,block) in out.chunks_mut(48).enumerate() {
            let t=i as f64*0.001;
            if s["kind"] == "sustain" {
                v.set_gate(if t>=0.1 && t<3.1 {1.} else {0.});
                if s["delayed"].as_bool().unwrap() && i==700 { v.set_source(&SourcePatch {vibrato_depth:Some(30.),..Default::default()}); }
            }
            v.render(block);
        }
        let bytes:Vec<u8> = out.iter().flat_map(|x| x.to_le_bytes()).collect();
        fs::write(dir.join(format!("{name}.f32")),bytes).unwrap();
        println!("{name}: {} samples",out.len());
    }
}
