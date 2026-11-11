//! A/B: the same sung syllables through the engine as its app drives it (A) and through the
//! module's wrapper (B; env TUNE sets tunable values). Writes <dir>/A.wav and <dir>/B.wav.   cargo run --release --example lyrab -- <dir> "words"
use chimera::anatomy::human::BodyMix;
use chimera::engine::Engine;
use chimera::phonetics::english::EnglishG2P;
use chimera::phonetics::sing::{plan_note_off, plan_note_on, NoteOnOptions};
use chimera::phonetics::syllables::lyrics_to_syllables;
use chimera_rack::*;
use std::io::{Read, Write};
const FS: f64 = 48000.0;
const BEAT: f64 = 0.7;

fn wav(path: &str, x: &[f32]) {
    let mut f = std::fs::File::create(path).unwrap();
    let n = (x.len() * 2) as u32;
    f.write_all(b"RIFF").unwrap(); f.write_all(&(36 + n).to_le_bytes()).unwrap(); f.write_all(b"WAVEfmt ").unwrap();
    f.write_all(&16u32.to_le_bytes()).unwrap(); f.write_all(&1u16.to_le_bytes()).unwrap(); f.write_all(&1u16.to_le_bytes()).unwrap();
    f.write_all(&48000u32.to_le_bytes()).unwrap(); f.write_all(&96000u32.to_le_bytes()).unwrap();
    f.write_all(&2u16.to_le_bytes()).unwrap(); f.write_all(&16u16.to_le_bytes()).unwrap();
    f.write_all(b"data").unwrap(); f.write_all(&n.to_le_bytes()).unwrap();
    let peak = x.iter().fold(1e-9f32, |m, s| m.max(s.abs()));
    for s in x { f.write_all(&(((s / peak) * 0.8 * 32767.0) as i16).to_le_bytes()).unwrap(); }
}
fn main() {
    let held: f64 = std::env::var("HELD").ok().and_then(|s| s.parse().ok()).unwrap_or(0.45);
    let dir = std::env::args().nth(1).expect("dir");
    let text = std::env::args().nth(2).expect("words");
    let nasal: f64 = std::env::var("NASAL").ok().and_then(|s| s.parse().ok()).unwrap_or(0.06);
    let mut dict = String::new();
    let gz: &[u8] = include_bytes!("../../../engine/public/data/cmudict.txt.gz");
    flate2::read::GzDecoder::new(gz).read_to_string(&mut dict).unwrap();
    let mut g = EnglishG2P::new();
    g.load_dict(&dict);
    let syls = lyrics_to_syllables(&text, &g);
    let body = BodyMix::new(0.0, 0.0);
    let midi: f64 = std::env::var("MIDI").ok().and_then(|s| s.parse().ok()).unwrap_or(48.0);
    let hz = 440.0 * 2f64.powf((midi - 69.0) / 12.0);
    let n = ((syls.len() as f64 * BEAT + 0.5) * FS) as usize;
    // A: the engine alone
    let mut e = Engine::new(FS);
    e.set_body(body);
    let mut a = vec![0f32; n];
    let mut was = false;
    for b in 0..n / 16 {
        let t = (b * 16) as f64 / FS;
        let i = (t / BEAT) as usize;
        let on = i < syls.len() && t - i as f64 * BEAT < held;
        if on && !was { e.set_gate(1.0); let gs = plan_note_on(&syls[i], midi, &NoteOnOptions { body, vibrato: 0.0, tag: 1.0, legato_from: None }); e.schedule(&gs, 0.0, false); }
        if !on && was { e.release(1.0); e.release(2.0); e.set_gate(0.0); let gs = plan_note_off(&syls[i.min(syls.len() - 1)], midi, body); e.schedule(&gs, 0.0, false); }
        was = on;
        e.render(&mut a[b * 16..b * 16 + 16]);
    }
    wav(&format!("{dir}/A.wav"), &a);
    // B: the wrapper, as the module calls it
    let c = std::ffi::CString::new(text.clone()).unwrap();
    // TUNE="0=0.3,5=10": tunable values by index (see T_* in lib.rs)
    let mut tune = TUNE_DEFAULT;
    if let Ok(t) = std::env::var("TUNE") {
        for kv in t.split(',') {
            if let Some((k, x)) = kv.split_once('=') { tune[k.parse::<usize>().unwrap()] = x.parse().unwrap(); }
        }
    }
    let l = chim_lyrics_new(c.as_ptr(), 0.0, tune.as_ptr());
    let v = chim_new(FS, 0.0, 0.0);
    chim_set_tune(v, tune.as_ptr());
    chim_set_lyrics(v, 1);
    for (i, key) in ["J0", "S0", "N1"].iter().enumerate() {
        if std::env::var(key).is_ok() { chim_debug(v, i as i32, if i == 2 { 1 } else { 0 }); }
    }
    if std::env::var("LEV0").is_ok() { chim_set_leveling(v, 0); }
    if std::env::var("LIM0").is_ok() { chim_set_limiter(v, 0); }
    let mut bb = vec![0f32; n];
    was = false;
    let mut k = 0;
    for b in 0..n / 16 {
        let t = (b * 16) as f64 / FS;
        let i = (t / BEAT) as usize;
        let on = i < syls.len() && t - i as f64 * BEAT < held;
        if on && !was { chim_note_on(v, l, k); k += 1; }
        if !on && was { chim_note_off(v); }
        was = on;
        let p = ChimParams { f0: hz, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: if on { 1.0 } else { 0.0 }, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * nasal, larynx: 0.0, ventricular: 0.0 };
        chim_set(v, &p);
        chim_render(v, bb[b * 16..].as_mut_ptr(), 16);
    }
    wav(&format!("{dir}/B.wav"), &bb);
    println!("syllables {}  dead {}", syls.len(), chim_is_dead(v));
}
