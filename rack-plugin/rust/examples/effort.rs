//! Level (RMS dBFS / peak) against EFFORT (knob 0.25..2 = lung pressure 2..16), vowel a.
//! The module's output is 6.25 V per unit, so peak 1.6 = 10 V.
use chimera_rack::*;
fn main() {
    let efforts = [0.25, 0.375, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0];
    println!("effort            {}", efforts.map(|e| format!("{e:10.3}")).join(""));
    for (female, f0, name) in [(0.0, 131.0, "male   C3"), (0.0, 262.0, "male   C4"), (1.0, 262.0, "female C4"), (1.0, 523.0, "female C5")] {
        for b in [0.15, 1.0] {
            print!("{name} b={b:4.2}  ");
            for e in efforts {
                let v = chim_new(48000.0, female, 0.0);
                let p = ChimParams { f0, pressure: 8.0 * e, breathiness: b, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
                let mut buf = vec![0f32; 72000];
                chim_render(v, buf.as_mut_ptr(), buf.len());
                chim_free(v);
                let t = &buf[24000..];
                let rms = (t.iter().map(|s| (*s as f64).powi(2)).sum::<f64>() / t.len() as f64).sqrt();
                let peak = t.iter().fold(0f32, |m, s| m.max(s.abs()));
                print!(" {:5.1}/{:.2}", 20.0 * rms.max(1e-9).log10(), peak);
            }
            println!();
        }
    }
}
