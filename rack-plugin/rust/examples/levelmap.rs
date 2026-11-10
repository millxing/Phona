//! RMS level (dBFS) against pitch (C2..C6 in minor thirds) for each body and vowel, at panel
//! defaults. Arg 1: "raw" = level correction off (the numbers for LEVEL_DB), else corrected.
//! Prints per-vowel levels, the vowel mean and the largest peak.
use chimera_rack::*;
fn main() {
    let raw = std::env::args().nth(1).as_deref() == Some("raw");
    for (female, name) in [(0.0, "male"), (1.0, "female")] {
        println!("{name}  ({})   i      e      a      o      u    | mean    peak", if raw { "raw" } else { "corrected" });
        let mut means = vec![];
        for k in 0..17 {
            let f0 = 65.406 * 2f64.powf(k as f64 / 4.0);
            print!("{f0:7.1} Hz ");
            let (mut sum, mut pk) = (0.0, 0f32);
            for vowel in 0..5 {
                let v = chim_new(48000.0, female, 0.0);
                chim_set_leveling(v, if raw { 0 } else { 1 });
                let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: vowel as f64, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * 0.06, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                let mut buf = vec![0f32; 72000];
                chim_render(v, buf.as_mut_ptr(), buf.len());
                chim_free(v);
                let t = &buf[36000..];
                let db = 10.0 * (t.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / t.len() as f64).max(1e-12).log10();
                pk = t.iter().fold(pk, |m, s| m.max(s.abs()));
                sum += db;
                print!(" {db:6.1}");
            }
            means.push(sum / 5.0);
            println!(" | {:6.1}  {pk:.2}", sum / 5.0);
        }
        println!("    [{}],", means.iter().map(|m| format!("{m:.1}")).collect::<Vec<_>>().join(", "));
    }
}
