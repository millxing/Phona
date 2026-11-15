//! Same input, same output on every platform. The engine does its math in plain f64 with a pinned
//! pure-Rust `libm`, so macOS arm64, Windows x64 and Linux x64 must give the same samples, bit for bit.
//! The expected checksums were recorded on macOS arm64. A deliberate change of the sound changes
//! them: then record new values on the Mac with
//!   PHONA_PRINT_CHECKSUMS=1 cargo test --release --test cross_platform -- --nocapture
//! and copy them here.
use chimera_rack::*;
use std::ffi::CString;

const RATE: f64 = 48000.0;
const BLOCK: usize = 64;

fn fnv1a(samples: &[f32]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for s in samples {
        for b in s.to_bits().to_le_bytes() {
            h ^= b as u64;
            h = h.wrapping_mul(0x100000001b3);
        }
    }
    h
}

fn params(gate: f64) -> ChimParams {
    ChimParams { f0: 196.0, pressure: 8.0, breathiness: 0.2, tension: 0.5, brightness: 0.3, roughness: 0.1,
        vib_depth: 30.0, vib_rate: 5.5, vowel: 2.0, gate, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0,
        ventricular: 0.0 }
}

fn render(v: *mut ChimVoice, p: &ChimParams, seconds: f64, out: &mut Vec<f32>) {
    let mut buf = [0f32; BLOCK];
    for _ in 0..(seconds * RATE / BLOCK as f64) as usize {
        chim_set(v, p);
        chim_render(v, buf.as_mut_ptr(), BLOCK);
        out.extend_from_slice(&buf);
    }
}

/// A held vowel that moves through the vowel space, then the gate closes.
fn sung(female: f64) -> Vec<f32> {
    let v = chim_new(RATE, female, 0.0);
    let mut out = Vec::new();
    let mut p = params(1.0);
    for k in 0..8 {
        p.vowel = k as f64 * 0.5;
        render(v, &p, 0.1, &mut out);
    }
    render(v, &params(0.0), 0.2, &mut out);
    chim_free(v);
    out
}

/// Lyrics: each gate sings the next syllable.
fn lyrics(female: f64) -> Vec<f32> {
    let v = chim_new(RATE, female, 0.0);
    let text = CString::new("hel-lo world, sing-ing").unwrap();
    let l = chim_lyrics_new(text.as_ptr(), female, std::ptr::null());
    assert!(!l.is_null());
    let mut out = Vec::new();
    chim_set_lyrics(v, 1);
    for i in 0..chim_lyrics_len(l) {
        chim_note_on(v, l, i);
        render(v, &params(1.0), 0.3, &mut out);
        chim_note_off(v);
        render(v, &params(0.0), 0.15, &mut out);
    }
    chim_set_lyrics(v, 0);
    chim_lyrics_free(l);
    chim_free(v);
    out
}

/// Beatbox: kick, snare, hat.
fn beat(female: f64) -> Vec<f32> {
    let v = chim_new(RATE, female, 0.0);
    let mut out = Vec::new();
    chim_set_beatbox(v, 1);
    for token in [1, 2, 3] {
        chim_beat_hit(v, token);
        render(v, &params(1.0), 0.35, &mut out);
    }
    chim_free(v);
    out
}

#[test]
fn same_samples_as_macos() {
    let cases: [(&str, fn(f64) -> Vec<f32>, f64, u64); 6] = [
        ("sung male", sung, 0.0, 0x391674826b56a44b),
        ("sung female", sung, 1.0, 0xa5d055ca33047af1),
        ("lyrics male", lyrics, 0.0, 0xb8a2569565f9c7ea),
        ("lyrics female", lyrics, 1.0, 0xf8a2bea22cbaf9fa),
        ("beatbox male", beat, 0.0, 0x022cae749a5426c4),
        ("beatbox female", beat, 1.0, 0x0224657ee6492d57),
    ];
    let print = std::env::var_os("PHONA_PRINT_CHECKSUMS").is_some();
    let mut failed = Vec::new();
    for (name, f, female, expected) in cases {
        let s = f(female);
        let rms = (s.iter().map(|x| (*x as f64) * (*x as f64)).sum::<f64>() / s.len() as f64).sqrt();
        assert!(s.iter().all(|x| x.is_finite()), "{name}: non-finite sample");
        assert!(rms > 1e-3, "{name}: silent (RMS {rms})");
        let h = fnv1a(&s);
        println!("{name}: {} samples, RMS {rms:.6}, checksum 0x{h:016x}", s.len());
        if print { println!("        (\"{name}\", ..., 0x{h:016x}),"); }
        if !print && h != expected { failed.push(format!("{name}: 0x{h:016x}, expected 0x{expected:016x} (RMS {rms:.6})")); }
    }
    assert!(failed.is_empty(), "output differs from macOS:\n{}", failed.join("\n"));
}
