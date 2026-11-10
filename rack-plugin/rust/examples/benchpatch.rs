//! CPU for the user's patch of 2026-11-09 (the Rack meter showed 28.5%): male, PITCH C4 with a
//! stepping V/OCT, VOWEL swept by a slow CV, NASAL 35%, FRY 23%. Arg 1 overrides NASAL, arg 2 FRY.
//! Prints CPU-time % of one core per second of audio, and the worst single second.
use chimera_rack::*;
fn cpu_secs() -> f64 {
    #[repr(C)] struct Tv { sec: i64, usec: i32 }
    #[repr(C)] struct Ru { ut: Tv, st: Tv, rest: [i64; 14] }
    extern "C" { fn getrusage(who: i32, r: *mut Ru) -> i32; }
    let mut r = Ru { ut: Tv { sec: 0, usec: 0 }, st: Tv { sec: 0, usec: 0 }, rest: [0; 14] };
    unsafe { getrusage(0, &mut r) };
    r.ut.sec as f64 + r.st.sec as f64 + (r.ut.usec as f64 + r.st.usec as f64) * 1e-6
}
fn main() {
    let nasal: f64 = std::env::args().nth(1).and_then(|s| s.parse().ok()).unwrap_or(0.346);
    let fry: f64 = std::env::args().nth(2).and_then(|s| s.parse().ok()).unwrap_or(0.233);
    let fs = 48000.0;
    let v = chim_new(fs, 0.0, 0.0);
    let (mut out, ext) = (0f32, 0f32);
    let steps = [0.0, 0.25, 0.583, -0.417, 0.0, 1.0, 0.333, -1.0];
    let mut per = vec![];
    let mut sum = 0f64;
    for sec in 0..16 {
        let c0 = cpu_secs();
        for i in 0..48000usize {
            if i % 16 == 0 {
                let t = sec as f64 + i as f64 / fs;
                let voct = steps[((t * 105.0 / 60.0 * 2.0) as usize) % 8];
                let vowel = (0.005 + 4.0 * (0.5 + 0.5 * (t * 1.1).sin())).clamp(0.0, 4.0);
                let p = ChimParams { f0: 261.63 * 2f64.powf(voct), pressure: 8.0 * 1.032, breathiness: 0.107, tension: 0.401, brightness: 0.611, roughness: 0.0, vib_depth: 13.7, vib_rate: 5.5, vowel, gate: 1.0, glide: 0.002 * 500f64.powf(0.194), fry, velum: -0.25 + 1.65 * nasal, larynx: -0.297, ventricular: 0.0 };
                chim_set(v, &p);
            }
            chim_render_ext(v, &ext, &mut out, 1);
            sum += (out as f64).powi(2);
        }
        per.push(100.0 * (cpu_secs() - c0));
    }
    println!("nasal {nasal} fry {fry}: mean {:.1}%  worst second {:.1}%  best {:.1}%  rms {:.1} dB dead {}", per.iter().sum::<f64>() / 16.0, per.iter().cloned().fold(0.0, f64::max), per.iter().cloned().fold(99.0, f64::min), 10.0 * (sum / 768000.0).log10(), chim_is_dead(v));
}
