//! Sounding pitch (autocorrelation) against FRY: flat to 0.5, then down to 48 Hz.
//! Above 0.3 the engine double-pulses, which can make the detector report an octave.
use chimera_rack::*;
fn pitch(buf: &[f32], fs: f64) -> f64 {
    let (lo, hi) = ((fs / 700.0) as usize, (fs / 35.0) as usize);
    let n = buf.len() - hi;
    let mut best = (0.0, lo);
    for lag in lo..hi {
        let (mut c, mut e) = (0f64, 0f64);
        for k in 0..n { c += buf[k] as f64 * buf[k + lag] as f64; e += (buf[k + lag] as f64).powi(2); }
        let r = c / e.max(1e-12).sqrt();
        if r > best.0 * 1.05 { best = (r, lag); }
    }
    fs / best.1 as f64
}
fn main() {
    let fs = 48000.0;
    println!("   f0 | fry 0.00   0.10   0.25   0.50   0.60   0.75   0.90   1.00  (Hz)");
    for f0 in [130.8, 261.6, 523.3] {
        print!("{f0:5.1} |    ");
        for fry in [0.0, 0.1, 0.25, 0.5, 0.6, 0.75, 0.9, 1.0] {
            let v = chim_new(fs, 0.0, 0.0);
            let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.0, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.02, fry, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
            chim_set(v, &p);
            let mut buf = vec![0f32; 48000];
            chim_render(v, buf.as_mut_ptr(), buf.len());
            chim_free(v);
            print!(" {:6.1}", pitch(&buf[24000..36000], fs));
        }
        println!();
    }
}
