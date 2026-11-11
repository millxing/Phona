//! Experiment: one word through the phrase planner (as speech), with switches toward the sung path.
//!   cargo run --release --example sp -- <out.wav> <word>     env: INTON=flat F0=131 VDUR=0.45 SRC=1 BASE=1 LIM=1
use chimera::anatomy::human::BodyMix;
use chimera::engine::Engine;
use chimera::phonetics::english::EnglishG2P;
use chimera::phonetics::planner::{is_vowel, plan_utterance, vowel_artic, PlanOptions};
use chimera::phonetics::Intonation;
use chimera::voice::human_voice::SourcePatch;
use std::io::{Read, Write};
const FS: f64 = 48000.0;
fn ev(k: &str) -> Option<f64> { std::env::var(k).ok().and_then(|s| s.parse().ok()) }
fn main() {
    let out = std::env::args().nth(1).expect("out");
    let word = std::env::args().nth(2).expect("word");
    let mut dict = String::new();
    let gz: &[u8] = include_bytes!("../../../engine/public/data/cmudict.txt.gz");
    flate2::read::GzDecoder::new(gz).read_to_string(&mut dict).unwrap();
    let mut g = EnglishG2P::new();
    g.load_dict(&dict);
    let body = BodyMix::new(0.0, 0.0);
    let mut segs = g.word(&word, false);
    if let Some(d) = ev("VDUR") { for s in segs.iter_mut() { if is_vowel(&s.ipa, body) { s.dur = Some(d); } } }
    let f0 = ev("F0").unwrap_or(112.0);
    let flat = std::env::var("INTON").is_ok_and(|s| s == "flat");
    if flat { for s in segs.iter_mut() { s.f0 = Some(f0); } }
    let plan = plan_utterance(&segs, &PlanOptions { body, t0: 0.05, f0: Some(f0), intonation: Some(if flat { Intonation::Flat } else { Intonation::Statement }), ..Default::default() });
    if ev("SEG").is_some() { for p in &plan.segments { print!("{} {:.3}  ", p.ipa, p.t1 - p.t0); } for s in &segs { print!(" [{} def {:.3}]", s.ipa, chimera::phonetics::inventory::phoneme(&s.ipa, body).map_or(0.0, |d| d.dur)); } println!(); }
    let mut e = Engine::new(FS);
    e.set_body(body);
    if ev("SRC").is_some() {
        e.set_source(&SourcePatch { f0: Some(f0), pressure: Some(8.0), breathiness: Some(0.15), tension: Some(0.5), brightness: Some(0.25), roughness: Some(0.1), ..Default::default() });
    } else {
        e.set_source(&SourcePatch::f0(f0));
    }
    if ev("BASE").is_some() { e.set_artic(&vowel_artic("ɑ", body).unwrap(), Some(true)); }
    if ev("LIM").is_some() { e.voice_mut().limiter = true; }
    let mut x;
    if ev("SPLIT").is_some() {
        // the live form: onset + open-ended vowel now, the coda as its own plan at the note-off
        let vi = segs.iter().position(|s| is_vowel(&s.ipa, body)).unwrap();
        let mut on: Vec<_> = segs[..=vi].to_vec();
        on[vi].dur = Some(30.0);
        let lt0 = ev("LT0").unwrap_or(0.05);
        let pon = plan_utterance(&on, &PlanOptions { body, t0: lt0, f0: Some(f0), intonation: Some(Intonation::Flat), open_end: Some(true), ..Default::default() });
        let gs: Vec<_> = pon.gestures.iter().map(|g| g.clone().tag(1.0)).collect();
        if ev("GATE").is_some() { e.set_gate(1.0); }
        e.schedule(&gs, 0.0, false);
        let off_at = pon.segments[vi].t0 + ev("VDUR").unwrap_or(0.45);
        let coda: Vec<_> = segs[vi + 1..].to_vec();
        let poff = plan_utterance(&coda, &PlanOptions { body, t0: ev("OT0").unwrap_or(-0.045), f0: Some(f0), intonation: Some(Intonation::Flat), ..Default::default() });
        let n = ((off_at + 0.7) * FS) as usize / 16 * 16;
        x = vec![0f32; n];
        let mut done = false;
        for b in 0..n / 16 {
            if !done && (b * 16) as f64 / FS >= off_at {
                done = true;
                e.release(1.0);
                e.set_gate(0.0);
                if !coda.is_empty() { e.schedule(&poff.gestures, 0.0, false); }
            }
            e.render(&mut x[b * 16..b * 16 + 16]);
        }
    } else {
        e.schedule(&plan.gestures, 0.0, false);
        let n = ((plan.end + 0.4) * FS) as usize / 16 * 16;
        x = vec![0f32; n];
        for b in 0..n / 16 { e.render(&mut x[b * 16..b * 16 + 16]); }
    }
    let mut f = std::fs::File::create(&out).unwrap();
    let nb = (x.len() * 2) as u32;
    f.write_all(b"RIFF").unwrap(); f.write_all(&(36 + nb).to_le_bytes()).unwrap(); f.write_all(b"WAVEfmt ").unwrap();
    f.write_all(&16u32.to_le_bytes()).unwrap(); f.write_all(&1u16.to_le_bytes()).unwrap(); f.write_all(&1u16.to_le_bytes()).unwrap();
    f.write_all(&48000u32.to_le_bytes()).unwrap(); f.write_all(&96000u32.to_le_bytes()).unwrap();
    f.write_all(&2u16.to_le_bytes()).unwrap(); f.write_all(&16u16.to_le_bytes()).unwrap();
    f.write_all(b"data").unwrap(); f.write_all(&nb.to_le_bytes()).unwrap();
    let peak = x.iter().fold(1e-9f32, |m, s| m.max(s.abs()));
    for s in &x { f.write_all(&(((s / peak) * 0.8 * 32767.0) as i16).to_le_bytes()).unwrap(); }
}
