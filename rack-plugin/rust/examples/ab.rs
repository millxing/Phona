// A/B a control through the exact C API the Rack module uses (set every 16 samples, render 1).
use chimera_rack::*;
fn run(female: f64, f: impl Fn(&mut ChimParams, usize)) -> Vec<f32> {
    let v = chim_new(48000.0, female, 0.0);
    let mut p = ChimParams { f0: 130.8, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
    let mut out = vec![0f32; 96000];
    for k in 0..out.len() { if k % 16 == 0 { f(&mut p, k); chim_set(v, &p); } chim_render(v, &mut out[k], 1); }
    chim_free(v); out
}
fn rms(x: &[f32]) -> f64 { 20.0 * ((x.iter().map(|v| (*v as f64).powi(2)).sum::<f64>() / x.len() as f64).sqrt()).log10() }
fn zc(x: &[f32]) -> f64 { x.windows(2).filter(|w| (w[0] < 0.0) != (w[1] < 0.0)).count() as f64 / (x.len() as f64 / 48000.0) / 2.0 }
fn pitch(x: &[f32]) -> f64 {
    // autocorrelation peak between 40 and 600 Hz
    let (lo, hi) = (80usize, 1200usize);
    let mut best = (0usize, f64::MIN);
    for lag in lo..hi { let c: f64 = x.iter().zip(&x[lag..]).map(|(a, b)| (*a * *b) as f64).sum(); if c > best.1 { best = (lag, c); } }
    48000.0 / best.0 as f64
}
fn main() {
    for (name, a, b) in [
        ("fry 0 vs 1", run(0.0, |p, _| p.fry = 0.0), run(0.0, |p, _| p.fry = 1.0)),
        ("tension 0 vs 1", run(0.0, |p, _| p.tension = 0.0), run(0.0, |p, _| p.tension = 1.0)),
        ("breathy 0 vs 1", run(0.0, |p, _| p.breathiness = 0.0), run(0.0, |p, _| p.breathiness = 1.0)),
    ] {
        let (a, b) = (&a[48000..], &b[48000..]);
        println!("{name}: rms {:.1} vs {:.1} dBFS, pitch {:.0} vs {:.0} Hz", rms(a), rms(b), pitch(&a[..4800]), pitch(&b[..4800]));
    }
    // glide: octave jump every 0.25 s, measure pitch settle via zero-cross in 10 ms windows after the jump
    for g in [0.002, 0.06, 1.0] {
        let o = run(0.0, move |p, k| { p.glide = g; p.f0 = if (k / 12000) % 2 == 0 { 130.8 } else { 261.6 }; });
        let w: Vec<String> = (0..6).map(|i| { let s = 12000 + i * 1920; format!("{:.0}", pitch(&o[s..s + 2400])) }).collect();
        println!("glide {g}s: pitch(zc) in 40ms steps after jump up: {}", w.join(" "));
    }
}
