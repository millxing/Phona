//! Offline test of the external input (talk box): a test sound is sent through the tract while
//! the vowel moves i → a → u → a → i. Writes WAV files to the directory in arg 1 and prints levels.
//! Sources: saw (110 Hz), a saw chord, white noise, and the folds alone for comparison.
use chimera_rack::*;
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
    for s in x { f.write_all(&((s / 1.6).clamp(-1.0, 1.0) * 32767.0).round().to_le_bytes()[..0]).unwrap(); f.write_all(&(((s / 1.6).clamp(-1.0, 1.0) * 32767.0) as i16).to_le_bytes()).unwrap(); }
}
fn main() {
    let dir = std::env::args().nth(1).expect("output dir");
    let secs = 8.0;
    let n = (secs * FS) as usize;
    let mut rng = 12345u32;
    let saw = |f: f64, i: usize| 2.0 * ((f * i as f64 / FS).fract()) - 1.0;
    for (female, body) in [(0.0, "male"), (1.0, "female")] {
        for (src, mix, gate) in [("folds", 0.0, 1.0), ("saw", 1.0, 1.0), ("chord", 1.0, 1.0), ("noise", 1.0, 1.0), ("saw_half", 0.5, 1.0), ("saw_gate_closed", 1.0, 0.0)] {
            let v = chim_new(FS, female, 0.0);
            chim_set_ext(v, mix);
            let mut out = vec![0f32; n];
            let mut inp = [0f32; 64];
            for b in 0..n / 64 {
                let t = (b * 64) as f64 / FS;
                // vowel path: i → a → u → a → i, 2 s per leg
                let legs = [0.0, 2.0, 4.0, 2.0, 0.0];
                let k = ((t / 2.0).floor() as usize).min(3);
                let vowel = legs[k] + (t / 2.0 - k as f64) * (legs[k + 1] - legs[k]);
                let p = ChimParams { f0: 110.0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel, gate, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * 0.06, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                for (j, x) in inp.iter_mut().enumerate() {
                    let i = b * 64 + j;
                    *x = match src {
                        "chord" => ((saw(110.0, i) + saw(138.59, i) + saw(164.81, i) + saw(220.5, i)) * 0.3) as f32,
                        "noise" => { rng = rng.wrapping_mul(1664525).wrapping_add(1013904223); (rng >> 8) as f32 / 8388608.0 - 1.0 }
                        _ => saw(110.0, i) as f32,
                    };
                }
                chim_render_ext(v, inp.as_ptr(), out[b * 64..].as_mut_ptr(), 64);
            }
            let dead = chim_is_dead(v);
            chim_free(v);
            let t = &out[24000..];
            let rms = (t.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / t.len() as f64).sqrt();
            let peak = t.iter().fold(0f32, |m, s| m.max(s.abs()));
            println!("{body:6} {src:13} rms {:6.1} dB  peak {peak:.2}  dead {dead}", 20.0 * rms.max(1e-9).log10());
            wav(&format!("{dir}/{body}_{src}.wav"), &out);
        }
    }
}
