//! A/B at panel defaults: engine limiter on (old build) against off (new build).
//! Prints RMS dB / peak / share of energy above 3 kHz (dB) for each.
use chimera_rack::*;
fn hf_db(t: &[f32]) -> f64 {
    // one-pole high-pass at 3 kHz, energy ratio
    let a = (-2.0 * std::f64::consts::PI * 3000.0 / 48000.0).exp();
    let (mut lp, mut hi, mut tot) = (0.0f64, 0.0f64, 0.0f64);
    for s in t { let x = *s as f64; lp = a * lp + (1.0 - a) * x; hi += (x - lp).powi(2); tot += x * x; }
    10.0 * (hi / tot.max(1e-18)).log10()
}
fn main() {
    for (female, name) in [(0.0, "male  "), (1.0, "female")] {
        for (f0, note) in [(65.4, "C2"), (98.0, "G2"), (130.8, "C3"), (196.0, "G3"), (261.6, "C4"), (392.0, "G4"), (523.3, "C5")] {
            print!("{name} {note}  ");
            for lim in [1, 0] {
                let v = chim_new(48000.0, female, 0.0);
                chim_set_limiter(v, lim);
                let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * 0.06, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                let mut buf = vec![0f32; 96000];
                chim_render(v, buf.as_mut_ptr(), buf.len());
                chim_free(v);
                let t = &buf[48000..];
                let rms = (t.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / t.len() as f64).sqrt();
                let peak = t.iter().fold(0f32, |m, s| m.max(s.abs()));
                print!(" | lim {} : {:6.1} dB  pk {:.2}  hf {:6.1}", if lim == 1 { "on " } else { "off" }, 20.0 * rms.max(1e-9).log10(), peak, hf_db(t));
            }
            println!();
        }
    }
}
