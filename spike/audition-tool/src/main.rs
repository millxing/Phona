use chimera::anatomy::human::{ArticPatch, BODY_MALE, BODY_FEMALE, BODY_CHILD};
use chimera::phonetics::planner::vowel_table;
use chimera::voice::human_voice::{HumanVoice, SourcePatch};
use std::{fs, path::Path};
const FS: f64 = 48000.;
fn write(dir: &Path, name: &str, samples: &[f32]) {
    let mut b = Vec::with_capacity(samples.len()*4);
    for x in samples { b.extend_from_slice(&x.to_le_bytes()); }
    fs::write(dir.join(format!("{name}.f32")), b).unwrap();
}
fn human(dir: &Path, name: &str, body: &str, ipa: &str, variant: &str, sweep: bool, pitch: bool) {
    let anatomy = match body { "female" => BODY_FEMALE, "child" => BODY_CHILD, _ => BODY_MALE };
    let table = &vowel_table()[body];
    let mut v = HumanVoice::new(FS, anatomy, 2);
    v.set_articulation(&table[ipa].artic, Some(true));
    // Fixed pitch controls: remove the default drift/jitter so the vowel comparison holds f0.
    let mut source = SourcePatch { f0: Some(110.), drift: Some(0.), roughness: Some(0.), ..Default::default() };
    match variant {
        "whisper" => { source.voiced=Some(false); source.breathiness=Some(0.8); },
        "breathy" => { source.breathiness=Some(0.8); source.tension=Some(0.2); },
        "rough" => { source.roughness=Some(0.85); source.fry=Some(0.65); source.tension=Some(0.3); },
        _ => ()
    }
    v.set_source(&source);
    let dur = if sweep {3.6} else {1.8};
    let mut out = vec![0.; (dur*FS) as usize];
    for (i, block) in out.chunks_mut(128).enumerate() {
        let t=i as f64*128./FS;
        if sweep {
            let progress=((t-0.35)/2.7).clamp(0.,1.)*2.;
            let (a,b,w)=if progress<1. {("ɑ","i",progress)} else {("i","u",progress-1.)};
            v.set_articulation(&ArticPatch::blend(&[(1.-w,table[a].artic),(w,table[b].artic)]),Some(true));
            if pitch { v.set_source(&SourcePatch::f0(110.*2_f64.powf(progress/2.))); }
        }
        v.set_gate(if t>=0.1 && t<dur-0.25 {1.} else {0.});
        v.render(block);
    }
    write(dir,name,&out);
}
fn main() {
    chimera::init();
    let arg=std::env::args().nth(1).expect("raw output directory");
    let dir=Path::new(&arg); fs::create_dir_all(dir).unwrap();
    for (name,ipa) in [("01a_vowel-a-male-110hz","ɑ"),("01b_vowel-i-male-110hz","i"),("01c_vowel-u-male-110hz","u"),("01d_vowel-e-male-110hz","e"),("01e_vowel-o-male-110hz","o")] {
        human(dir,name,"male",ipa,"",false,false);
    }
    human(dir,"02a_vowel-glide-a-i-u-110hz","male","ɑ","",true,false);
    human(dir,"02b_vowel-and-pitch-glide-110-to-220hz","male","ɑ","",true,true);
    for body in ["male","female","child"] { human(dir,&format!("07_vowel-a-{body}-110hz"),body,"ɑ","",false,false); }
    for variant in ["whisper","breathy","rough"] { human(dir,&format!("08_vowel-a-{variant}"),"male","ɑ",variant,false,false); }
}
