//! Level (RMS dBFS) and peak for breathiness x lung pressure. `chim_set` scales the pressure and
//! raises the aspiration noise with breathiness (`breath_comp`); the 8.0 column shows the compensated
//! result the module gives, which should stay near the level of the 0.15 row.
//! Usage: cargo run --release --example breathy [vowel 0..4]
use chimera_rack::*;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let vowel: f64 = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(2.0);
    let pressures = [8.0, 10.0, 12.0, 14.0, 16.0, 18.0, 20.0];
    for (female, f0, name) in [(0.0, 131.0, "male"), (1.0, 262.0, "female")] {
        println!("{name} f0={f0} vowel={vowel}  rows: breathy, cols: pressure {pressures:?}  (rms dB / peak)");
        for b in [0.0, 0.15, 0.5, 0.75, 1.0, 1.25, 1.5] {
            print!("{b:5.2} ");
            for pr in pressures {
                let v = chim_new(48000.0, female, 0.0);
                let p = ChimParams { f0, pressure: pr, breathiness: b, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                let mut buf = vec![0f32; 72000];
                chim_render(v, buf.as_mut_ptr(), buf.len());
                let tail = &buf[24000..];
                let rms = (tail.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / tail.len() as f64).sqrt();
                let peak = tail.iter().fold(0f32, |m, s| m.max(s.abs()));
                let dead = chim_is_dead(v) != 0;
                print!(" {:6.1}/{:.2}{}", 20.0 * rms.max(1e-9).log10(), peak, if dead { "!" } else { "" });
                chim_free(v);
            }
            println!();
        }
    }
}
