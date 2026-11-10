//! Male against female at panel defaults: dumps raw f32 (1 s, 48 kHz) per body / note / vowel
//! to the directory in arg 1, for spectrum analysis.
use chimera_rack::*;
use std::io::Write;
fn main() {
    let dir = std::env::args().nth(1).expect("output dir");
    for (female, name) in [(0.0, "m"), (1.0, "f")] {
        for (f0, note) in [(130.8, "C3"), (196.0, "G3"), (261.6, "C4"), (392.0, "G4")] {
            for vowel in [0.0, 2.0, 4.0] {
                let v = chim_new(48000.0, female, 0.0);
                let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * 0.06, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                let mut buf = vec![0f32; 96000];
                chim_render(v, buf.as_mut_ptr(), buf.len());
                chim_free(v);
                let mut f = std::fs::File::create(format!("{dir}/{name}_{note}_{vowel}.f32")).unwrap();
                for s in &buf[48000..] { f.write_all(&s.to_le_bytes()).unwrap(); }
            }
        }
    }
}
