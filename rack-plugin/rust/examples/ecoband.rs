//! Eco against full: averaged spectrum (Welch, 2048-point Hann frames) in third-octave bands (scratch).
use chimera_rack::*;
fn render(eco: i32, female: f64, f0: f64, vowel: f64) -> Vec<f32> {
    let v = chim_new_eco(48000.0, female, 0.0, eco);
    let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
    let mut out = vec![0f32; 96000];
    for c in out.chunks_mut(16) { chim_set(v, &p); chim_render(v, c.as_mut_ptr(), c.len()); }
    chim_free(v);
    out[24000..].to_vec()
}
fn bands(x: &[f32]) -> Vec<f64> {
    let n = 2048;
    let centers: Vec<f64> = (0..20).map(|i| 125.0 * 2f64.powf(i as f64 / 3.0)).collect(); // 125 Hz .. 10 kHz
    let mut e = vec![0f64; centers.len()];
    let mut start = 0;
    while start + n <= x.len() {
        for k in 1..(n / 2) {
            let f = k as f64 * 48000.0 / n as f64;
            let Some(b) = centers.iter().position(|c| f >= c / 2f64.powf(1.0 / 6.0) && f < c * 2f64.powf(1.0 / 6.0)) else { continue };
            let w = std::f64::consts::TAU * k as f64 / n as f64;
            let (mut re, mut im) = (0.0, 0.0);
            for i in 0..n {
                let s = x[start + i] as f64 * (0.5 - 0.5 * (std::f64::consts::TAU * i as f64 / n as f64).cos());
                re += s * (w * i as f64).cos();
                im += s * (w * i as f64).sin();
            }
            e[b] += re * re + im * im;
        }
        start += n;
    }
    e.iter().map(|v| 10.0 * (v + 1e-20).log10()).collect()
}
fn main() {
    for (female, f0, name) in [(0.0, 100.0, "male 100"), (0.0, 200.0, "male 200"), (1.0, 200.0, "female 200")] {
        for (vi, vn) in [(0.0, "i"), (2.0, "a"), (4.0, "u")] {
            let a = bands(&render(0, female, f0, vi));
            let b = bands(&render(1, female, f0, vi));
            let d: Vec<String> = a.iter().zip(&b).map(|(x, y)| format!("{:+4.0}", y - x)).collect();
            println!("{name:10} [{vn}] eco-full dB, 125 Hz..10 kHz by 1/3 oct: {}", d.join(""));
        }
    }
}
