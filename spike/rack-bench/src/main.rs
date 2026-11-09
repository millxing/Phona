//! Rack-shaped benchmark of the Chimera engine.
//! Rack calls process() once per sample; Rack's own engine blocks are typically small.
//! We render through `Engine` (what a module would wrap) with:
//!   - sample rates 44.1k / 48k / 96k (Engine picks oversample 2 below 60k, 1 above)
//!   - call sizes 1 / 16 / 128 samples
//!   - static controls vs continuous modulation (pitch + tongue/jaw/lip LFOs) applied
//!     every call (block=1 => every sample) or every 32 samples
//!   - flush-to-zero off/on (aarch64 FPCR.FZ), comparing speed and output
//! Timing is measured per 128-sample window (worst case matters for Rack's audio callback).
use chimera::anatomy::human::{ArticKey, ArticPatch, BodyMix};
use chimera::engine::Engine;
use chimera::voice::human_voice::SourcePatch;
use std::time::Instant;

#[cfg(target_arch = "aarch64")]
fn set_ftz(on: bool) {
    unsafe {
        let mut fpcr: u64;
        std::arch::asm!("mrs {}, fpcr", out(reg) fpcr);
        if on { fpcr |= 1 << 24 } else { fpcr &= !(1 << 24) }
        std::arch::asm!("msr fpcr, {}", in(reg) fpcr);
    }
}
#[cfg(not(target_arch = "aarch64"))]
fn set_ftz(_on: bool) {}

#[derive(Clone, Copy, PartialEq)]
enum Modu { Static, EverySample, Every32 }

struct Res { cpu_pct: f64, p50_us: f64, p99_us: f64, max_us: f64, budget_us: f64, nan: bool, out: Vec<f32> }

fn run(fs: f64, call: usize, modu: Modu, ftz: bool, child: bool, secs: f64) -> Res {
    set_ftz(ftz);
    let mut e = Engine::new(fs);
    if child { e.set_body(BodyMix::CHILD); } else { e.set_body(BodyMix::MALE); }
    e.set_source(&SourcePatch::f0(110.0));
    e.set_artic(&ArticPatch::default().with(ArticKey::Jaw, 0.5), None);
    e.set_gate(1.0);
    let n = (fs * secs) as usize;
    let win = 128usize;
    let mut out = vec![0.0f32; n];
    let mut times = Vec::with_capacity(n / win + 1);
    let total0 = Instant::now();
    let mut i = 0;
    let mut wstart = Instant::now();
    let mut wcount = 0;
    while i < n {
        let len = call.min(n - i);
        let t = i as f64 / fs;
        let apply = match modu { Modu::Static => false, Modu::EverySample => true, Modu::Every32 => i % 32 < len };
        if apply {
            // ~1 V/oct style pitch LFO + slow tongue/jaw/lip movement
            let f0 = 110.0 * (2f64).powf(0.5 * (2.0 * std::f64::consts::PI * 0.7 * t).sin());
            e.set_source(&SourcePatch::f0(f0));
            let s = |hz: f64| 0.5 + 0.4 * (2.0 * std::f64::consts::PI * hz * t).sin();
            let p = ArticPatch::default()
                .with(ArticKey::TbX, s(0.31))
                .with(ArticKey::TbY, s(0.53))
                .with(ArticKey::Jaw, s(0.23))
                .with(ArticKey::LipAp, s(0.41));
            e.set_artic(&p, None);
        }
        e.render(&mut out[i..i + len]);
        i += len;
        wcount += len;
        if wcount >= win {
            times.push(wstart.elapsed().as_secs_f64());
            wstart = Instant::now();
            wcount = 0;
        }
    }
    let total = total0.elapsed().as_secs_f64();
    set_ftz(false);
    times.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let q = |p: f64| times[((times.len() - 1) as f64 * p) as usize] * 1e6;
    Res {
        cpu_pct: 100.0 * total / secs,
        p50_us: q(0.5),
        p99_us: q(0.99),
        max_us: times.last().unwrap() * 1e6,
        budget_us: win as f64 / fs * 1e6,
        nan: out.iter().any(|x| !x.is_finite()),
        out,
    }
}

fn rms(x: &[f32]) -> f64 { (x.iter().map(|v| (*v as f64).powi(2)).sum::<f64>() / x.len() as f64).sqrt() }

fn main() {
    let secs = 4.0;
    // warm-up
    run(48000.0, 128, Modu::Static, false, false, 0.5);
    println!("{:<8}{:<6}{:<12}{:<5}{:<6}{:>8}{:>10}{:>10}{:>10}{:>10}{:>6}", "fs", "call", "modulation", "ftz", "body", "cpu%", "p50us", "p99us", "maxus", "budget", "nan");
    let mut cases = vec![];
    for fs in [44100.0, 48000.0, 96000.0] {
        for call in [1usize, 16, 128] {
            for modu in [Modu::Static, Modu::EverySample, Modu::Every32] {
                if call > 1 && modu == Modu::EverySample { continue; }
                cases.push((fs, call, modu, false, false));
            }
        }
    }
    cases.push((48000.0, 1, Modu::Every32, true, false));
    cases.push((48000.0, 1, Modu::Every32, false, true));
    cases.push((96000.0, 1, Modu::Every32, true, false));
    for (fs, call, modu, ftz, child) in cases {
        let r = run(fs, call, modu, ftz, child, secs);
        let m = match modu { Modu::Static => "static", Modu::EverySample => "per-sample", Modu::Every32 => "every32" };
        println!("{:<8}{:<6}{:<12}{:<5}{:<6}{:>8.1}{:>10.1}{:>10.1}{:>10.1}{:>10.1}{:>6}", fs, call, m, ftz, if child { "child" } else { "male" }, r.cpu_pct, r.p50_us, r.p99_us, r.max_us, r.budget_us, r.nan);
    }
    // block-size independence (static controls, so control timing is identical) + FTZ effect,
    // including a gate-off tail where decaying values could go subnormal
    let tail = |call: usize, ftz: bool| {
        set_ftz(ftz);
        let mut e = Engine::new(48000.0);
        e.set_source(&SourcePatch::f0(110.0));
        e.set_gate(1.0);
        let n = 48000usize;
        let mut out = vec![0.0f32; 3 * n];
        let mut i = 0;
        let t0 = Instant::now();
        let mut t_tail = 0.0;
        while i < out.len() {
            if i == n { e.set_gate(0.0); }
            if i == 2 * n { t_tail = t0.elapsed().as_secs_f64(); }
            let len = call.min(out.len() - i);
            e.render(&mut out[i..i + len]);
            i += len;
        }
        let tot = t0.elapsed().as_secs_f64();
        set_ftz(false);
        (out, tot - t_tail)
    };
    let (a, _) = tail(1, false);
    let (b, _) = tail(128, false);
    let (c, tc) = tail(1, true);
    let (_, ta) = tail(1, false);
    let diff = |x: &[f32], y: &[f32]| x.iter().zip(y).map(|(p, q)| (p - q).abs()).fold(0f32, f32::max);
    println!("\nmax |call=1 - call=128| = {:e} (signal rms {:.4})", diff(&a, &b), rms(&a[..48000]));
    println!("max |ftz off - ftz on| = {:e}; last-second (silent tail) cpu: ftz off {:.1}% / on {:.1}%", diff(&a, &c), ta * 100.0, tc * 100.0);
    println!("tail peak |x| in last second: {:e}", a[96000..].iter().fold(0f32, |m, v| m.max(v.abs())));
}
