//! Live sweep: NASAL 0 → 1 → 0 (1 s each way) while the vowel steps through 9 positions, both
//! bodies. Reports dead voices, the loudest sample-to-sample jump (clicks) and level range per
//! 50 ms frame.
use chimera_rack::*;
fn main() {
    for (female, f0, name) in [(0.0, 130.8, "male C3"), (1.0, 261.6, "female C4")] {
        for vi in 0..=8 {
            let vowel = vi as f64 * 0.5;
            let v = chim_new(48000.0, female, 0.0);
            let n = 48000 * 3;
            let mut out = vec![0f32; n];
            for b in 0..n / 64 {
                let t = (b * 64) as f64 / 48000.0;
                let kn = if t < 0.5 { 0.0 } else if t < 1.5 { t - 0.5 } else if t < 2.5 { 2.5 - t } else { 0.0 };
                let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * kn, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                chim_render(v, out[b * 64..].as_mut_ptr(), 64);
            }
            let dead = chim_is_dead(v);
            chim_free(v);
            let t = &out[14400..];
            let jump = t.windows(2).fold(0f32, |m, w| m.max((w[1] - w[0]).abs()));
            let pk = t.iter().fold(0f32, |m, s| m.max(s.abs()));
            let (mut lo, mut hi) = (f64::INFINITY, f64::NEG_INFINITY);
            for fr in t.chunks(2400) { let db = 10.0 * (fr.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / fr.len() as f64).max(1e-12).log10(); lo = lo.min(db); hi = hi.max(db); }
            println!("{name} vowel {vowel:3.1}: dead {dead}  peak {pk:.2}  max jump {jump:.3}  frame level {lo:6.1} .. {hi:5.1} dB");
        }
    }
}
