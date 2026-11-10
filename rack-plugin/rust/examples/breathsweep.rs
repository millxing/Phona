//! Sweep BREATHY 0 -> 1.5 on a sounding voice (as a knob turn in Rack) and print the level in
//! three bands every 0.5 s: low (<500 Hz), mid (500 Hz-3 kHz), high (>3 kHz, where breath noise is).
//! Usage: cargo run --release --example breathsweep [sample_rate]
use chimera_rack::*;

fn db(e: f64, n: usize) -> f64 { 10.0 * (e / n as f64).max(1e-12).log10() }

fn main() {
    let fs: f64 = std::env::args().nth(1).and_then(|s| s.parse().ok()).unwrap_or(48000.0);
    for (female, f0, name) in [(0.0, 131.0, "male"), (1.0, 262.0, "female")] {
        println!("{name} fs={fs}:  breathy   total    low    mid   high (dB)");
        let v = chim_new(fs, female, 0.0);
        let mut p = ChimParams { f0, pressure: 8.0, breathiness: 0.0, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
        let total = (8.0 * fs) as usize; // 1 s settle + 7 s sweep
        let block = (0.5 * fs) as usize;
        let (a1, a2) = (1.0 - (-6.2832 * 500.0 / fs).exp(), 1.0 - (-6.2832 * 3000.0 / fs).exp());
        let (mut l1, mut l2) = (0f64, 0f64);
        let (mut et, mut el, mut em, mut eh) = (0f64, 0f64, 0f64, 0f64);
        for k in 0..total {
            if k % 16 == 0 {
                p.breathiness = (1.5 * (k as f64 / fs - 1.0) / 6.5).clamp(0.0, 1.5);
                chim_set(v, &p);
            }
            let mut s = 0f32;
            chim_render(v, &mut s, 1);
            let x = s as f64;
            l1 += a1 * (x - l1);
            l2 += a2 * (x - l2);
            et += x * x; el += l1 * l1; em += (l2 - l1).powi(2); eh += (x - l2).powi(2);
            if (k + 1) % block == 0 {
                println!("            {:5.2}   {:6.1} {:6.1} {:6.1} {:6.1}", p.breathiness, db(et, block), db(el, block), db(em, block), db(eh, block));
                et = 0.0; el = 0.0; em = 0.0; eh = 0.0;
            }
        }
        chim_free(v);
    }
}
