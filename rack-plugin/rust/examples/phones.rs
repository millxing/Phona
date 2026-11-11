//! Prints the phonemes of each syllable of a text.   cargo run --release --example phones -- "words"
use chimera::phonetics::english::EnglishG2P;
use chimera::phonetics::syllables::lyrics_to_syllables;
use std::io::Read;
fn main() {
    let mut text = String::new();
    let gz: &[u8] = include_bytes!("../../../engine/public/data/cmudict.txt.gz");
    flate2::read::GzDecoder::new(gz).read_to_string(&mut text).unwrap();
    let mut g = EnglishG2P::new();
    g.load_dict(&text);
    for t in std::env::args().skip(1) {
        for s in lyrics_to_syllables(&t, &g) {
            let j = |v: &Vec<chimera::phonetics::Segment>| v.iter().map(|x| x.ipa.clone()).collect::<Vec<_>>().join(" ");
            if std::env::var("GEST").is_ok() {
                let o = chimera::phonetics::sing::NoteOnOptions { body: Default::default(), vibrato: 0.0, tag: 1.0, legato_from: None };
                for g in chimera::phonetics::sing::plan_note_on(&s, 57.0, &o) {
                    println!("    {:8} {:6.3}..{:6.3} str {:3.1}  {}", g.label_str(), g.t0, g.t1.min(9.0), g.strength, g.targets.iter().map(|(k, x)| format!("{}={:.2}", k.name(), x)).collect::<Vec<_>>().join(" "));
                }
                for g in chimera::phonetics::sing::plan_note_off(&s, 57.0, Default::default()) {
                    println!("  off {:8} {:6.3}..{:6.3} in {:.3} out {:.3} str {:3.1}  {}", g.label_str(), g.t0, g.t1.min(9.0), g.ramp_in, g.ramp_out, g.strength, g.targets.iter().map(|(k, x)| format!("{}={:.2}", k.name(), x)).collect::<Vec<_>>().join(" "));
                }
            }
            println!("{:8} onset [{}]  nucleus [{}]  coda [{}]{}", s.text, j(&s.onset), s.nucleus.ipa, j(&s.coda), if s.melisma == Some(true) { "  (melisma)" } else { "" });
        }
    }
}
