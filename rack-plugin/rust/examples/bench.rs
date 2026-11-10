//! CPU benchmark: seconds of audio per second of CPU, per-sample calls as the module makes them
//! (chim_set every 16 samples, chim_render_ext one sample at a time). Prints % of one core.
//! Args: seconds (default 20), sample rate (default 48000). Also prints a checksum of the output
//! so an optimisation can be checked for bit-identical results. Arg 3 = "held": the vowel and
//! NASAL stand still (the default moves both all the time, the costly case).
use chimera_rack::*;
use std::time::Instant;
/// CPU time of this thread's process (user + system), in seconds: not disturbed by other programs
/// taking the core away, as wall-clock time is.
fn cpu_secs() -> f64 {
    #[repr(C)]
    struct Tv { sec: i64, usec: i32 }
    #[repr(C)]
    struct Ru { ut: Tv, st: Tv, rest: [i64; 14] }
    extern "C" { fn getrusage(who: i32, r: *mut Ru) -> i32; }
    let mut r = Ru { ut: Tv { sec: 0, usec: 0 }, st: Tv { sec: 0, usec: 0 }, rest: [0; 14] };
    unsafe { getrusage(0, &mut r) };
    r.ut.sec as f64 + r.st.sec as f64 + (r.ut.usec as f64 + r.st.usec as f64) * 1e-6
}
fn main() {
    let secs: f64 = std::env::args().nth(1).and_then(|s| s.parse().ok()).unwrap_or(20.0);
    let fs: f64 = std::env::args().nth(2).and_then(|s| s.parse().ok()).unwrap_or(48000.0);
    let held = std::env::args().nth(3).as_deref() == Some("held");
    for (female, f0, name) in [(0.0, 130.8, "male C3"), (1.0, 261.6, "female C4")] {
        let v = chim_new(fs, female, 0.0);
        let n = (secs * fs) as usize;
        let (mut sum, mut hash) = (0f64, 0u64);
        let t0 = Instant::now();
        let c0 = cpu_secs();
        let mut out = 0f32;
        let ext = 0f32;
        for i in 0..n {
            if i % 16 == 0 {
                let t = if held { 0.0 } else { i as f64 / fs };
                let p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 30.0, vib_rate: 5.5, vowel: 2.0 + 2.0 * (t * 0.7).sin(), gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.25 + 1.65 * (0.3 + 0.3 * (t * 0.4).sin()), larynx: 0.0, ventricular: 0.0 };
                chim_set(v, &p);
            }
            chim_render_ext(v, &ext, &mut out, 1);
            sum += (out as f64) * (out as f64);
            hash = hash.wrapping_mul(0x100000001b3).wrapping_add(out.to_bits() as u64);
        }
        let _wall = t0.elapsed().as_secs_f64();
        let dt = cpu_secs() - c0;
        let dead = chim_is_dead(v);
        chim_free(v);
        println!("{name}: {:5.2}% of one core  ({:.1}x real time)  rms {:.1} dB  dead {dead}  hash {hash:016x}", 100.0 * dt / secs, secs / dt, 10.0 * (sum / n as f64).log10());
    }
}
