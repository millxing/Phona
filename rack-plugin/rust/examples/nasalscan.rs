//! Level (RMS dBFS / peak) against the NASAL knob (0..1) for each vowel position and body, and
//! the velum cap the wrapper applies. Shows dead voices. Male C3 / female C4, panel defaults.
use chimera_rack::*;
fn main() {
    let knobs = [0.0, 0.06, 0.25, 0.5, 0.75, 1.0];
    for (female, f0, name) in [(0.0, 130.8, "male C3"), (1.0, 261.6, "female C4")] {
        println!("{name}   nasal {}   | cap", knobs.map(|k| format!("{k:10.2}")).join(""));
        for vi in 0..=8 {
            let vowel = vi as f64 * 0.5;
            print!("  vowel {vowel:3.1}      ");
            let mut cap = 0.0;
            for kn in knobs {
                let v = chim_new(48000.0, female, 0.0);
                let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * kn, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                let mut buf = vec![0f32; 72000];
                chim_render(v, buf.as_mut_ptr(), buf.len());
                cap = chim_velum_cap(v, vowel);
                let dead = chim_is_dead(v);
                chim_free(v);
                let t = &buf[36000..];
                let db = 10.0 * (t.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / t.len() as f64).max(1e-12).log10();
                let pk = t.iter().fold(0f32, |m, s| m.max(s.abs()));
                print!(" {db:5.1}/{pk:.2}{}", if dead != 0 { "D" } else { " " });
            }
            println!(" | {cap:.2}");
        }
    }
}
