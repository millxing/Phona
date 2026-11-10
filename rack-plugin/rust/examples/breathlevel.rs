//! Level change (dB, against breathiness 0.15) and peak for BREATHY 0.5 / 1.0 / 1.5, per body and
//! note, vowel a, with level correction on. Shows where the breath compensation over/undershoots.
use chimera_rack::*;
fn main() {
    println!("                 b=0.15        0.5          1.0          1.5");
    for (female, name) in [(0.0, "male  "), (1.0, "female")] {
        for (f0, note) in [(65.4, "C2"), (98.0, "G2"), (130.8, "C3"), (196.0, "G3"), (261.6, "C4"), (392.0, "G4"), (523.3, "C5")] {
            print!("{name} {note}  ");
            let mut base = 0.0;
            for b in [0.15, 0.5, 1.0, 1.5] {
                let v = chim_new(48000.0, female, 0.0);
                let p = ChimParams { f0, pressure: 8.0, breathiness: b, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * 0.06, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                let mut buf = vec![0f32; 72000];
                chim_render(v, buf.as_mut_ptr(), buf.len());
                chim_free(v);
                let t = &buf[36000..];
                let db = 10.0 * (t.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / t.len() as f64).max(1e-12).log10();
                let pk = t.iter().fold(0f32, |m, s| m.max(s.abs()));
                if b == 0.15 { base = db; print!(" {db:6.1}/{pk:.2}"); } else { print!("  {:+5.1}/{pk:.2}", db - base); }
            }
            println!();
        }
    }
}
