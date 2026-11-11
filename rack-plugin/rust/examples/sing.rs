//! Offline test of lyrics mode: gates and pitches as a sequencer would send them, sung to a text.
//! Writes WAV files to the directory in arg 1.   cargo run --release --example sing -- <dir>
use chimera_rack::*;
use std::ffi::CString;
use std::io::Write;
const FS: f64 = 48000.0;
fn wav(path: &str, x: &[f32]) {
    let mut f = std::fs::File::create(path).unwrap();
    let n = (x.len() * 2) as u32;
    f.write_all(b"RIFF").unwrap(); f.write_all(&(36 + n).to_le_bytes()).unwrap(); f.write_all(b"WAVEfmt ").unwrap();
    f.write_all(&16u32.to_le_bytes()).unwrap(); f.write_all(&1u16.to_le_bytes()).unwrap(); f.write_all(&1u16.to_le_bytes()).unwrap();
    f.write_all(&48000u32.to_le_bytes()).unwrap(); f.write_all(&96000u32.to_le_bytes()).unwrap();
    f.write_all(&2u16.to_le_bytes()).unwrap(); f.write_all(&16u16.to_le_bytes()).unwrap();
    f.write_all(b"data").unwrap(); f.write_all(&n.to_le_bytes()).unwrap();
    for s in x { f.write_all(&(((s / 1.6).clamp(-1.0, 1.0) * 32767.0) as i16).to_le_bytes()).unwrap(); }
}
fn main() {
    let dir = std::env::args().nth(1).expect("output dir");
    // (text, midi notes, beat seconds, gate share of the beat)
    let twinkle = [60, 60, 67, 67, 69, 69, 67, 67, 65, 65, 64, 64, 62, 62, 60, 60];
    let cases: [(&str, &str, &[i32], f64, f64); 4] = [
        ("twinkle", "twinkle twinkle little star, how I wonder what you are", &twinkle[..14], 0.5, 0.8),
        ("twinkle_fast", "twinkle twinkle little star, how I wonder what you are", &twinkle[..14], 0.25, 0.5),
        ("twinkle_trig", "twinkle twinkle little star, how I wonder what you are", &twinkle[..14], 0.5, 0.01),
        ("words", "the quick brown fox jumps over the lazy dog", &[55, 57, 59, 60, 62, 60, 59, 57, 55, 55, 55], 0.45, 0.85),
    ];
    for (female, body) in [(0.0, "male"), (1.0, "female")] {
        for (name, text, notes, beat, share) in cases {
            let c = CString::new(text).unwrap();
            let l = chim_lyrics_new(c.as_ptr(), female, std::ptr::null());
            let n_syl = chim_lyrics_len(l);
            let v = chim_new(FS, female, 0.0);
            chim_set_lyrics(v, 1);
            if std::env::var("RAW").is_ok() { chim_set_leveling(v, 0); }
            if std::env::var("LIM").is_ok() { chim_set_limiter(v, 1); }
            let total = ((notes.len() as f64 * beat + 1.0) * FS) as usize;
            let mut out = vec![0f32; total];
            let mut was = false;
            let mut k = 0usize;
            let shift = if female > 0.5 { 0 } else { -12 };
            for b in 0..total / 16 {
                let t = (b * 16) as f64 / FS;
                let i = (t / beat) as usize;
                let on = i < notes.len() && (t - i as f64 * beat) < beat * share;
                if on && !was { chim_note_on(v, l, k % n_syl); k += 1; }
                if !on && was { chim_note_off(v); }
                was = on;
                let midi = notes[i.min(notes.len() - 1)] + shift;
                let f0 = 440.0 * 2f64.powf((midi as f64 - 69.0) / 12.0);
                let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: if on { 1.0 } else { 0.0 }, glide: 0.03, fry: 0.0, velum: -0.25 + 1.65 * 0.06, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                chim_render(v, out[b * 16..].as_mut_ptr(), 16);
            }
            let dead = chim_is_dead(v);
            chim_set_lyrics(v, 0);
            chim_free(v);
            chim_lyrics_free(l);
            let rms = (out.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / out.len() as f64).sqrt();
            let peak = out.iter().fold(0f32, |m, s| m.max(s.abs()));
            println!("{body:6} {name:13} syllables {n_syl:2}  rms {:6.1} dB  peak {peak:.2}  dead {dead}", 20.0 * rms.max(1e-9).log10());
            wav(&format!("{dir}/{body}_{name}.wav"), &out);
        }
    }
}
