//! Source-only listening comparison. Writes unnormalized mono f32 at 48 kHz.
//! cargo run --release --example female_audition -- ../../../audition/female-source-01
use chimera_rack::*;
use std::{ffi::CString, io::Write};
const FS: f64 = 48000.0;

fn render(vowel: f64, text: Option<&str>, notes: &[i32], beat: f64, source: [f64; 3]) -> Vec<f32> {
    let v = chim_new(FS, 1.0, 0.0);
    assert!(!v.is_null());
    let c = CString::new(text.unwrap_or("")).unwrap();
    let l = chim_lyrics_new(c.as_ptr(), 1.0, std::ptr::null());
    if text.is_some() {
        assert_eq!(chim_lyrics_len(l), notes.len(), "unexpected syllable count: {text:?}");
        chim_set_lyrics(v, 1);
    }
    let total = ((notes.len() as f64 * beat + 0.6) * FS) as usize / 16 * 16;
    let mut out = vec![0f32; total];
    let mut was = false;
    for b in 0..total / 16 {
        let t = (b * 16) as f64 / FS;
        let i = (t / beat) as usize;
        let on = i < notes.len() && t - (i as f64 * beat) < beat * 0.75;
        if text.is_some() {
            if on && !was { chim_note_on(v, l, i); }
            if !on && was { chim_note_off(v); }
        }
        was = on;
        let p = ChimParams {
            f0: 440.0 * 2f64.powf((notes[i.min(notes.len()-1)] as f64 - 69.0) / 12.0),
            pressure: 8.0, breathiness: source[2], tension: source[0], brightness: source[1],
            roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel,
            gate: if on {1.0} else {0.0}, glide: 0.03, fry: 0.0,
            velum: -0.25 + 1.65 * 0.06, larynx: 0.0, ventricular: 0.0,
        };
        chim_set(v, &p);
        chim_render(v, out[b*16..].as_mut_ptr(), 16);
    }
    assert_eq!(chim_is_dead(v), 0, "voice failed");
    assert!(out.iter().all(|s| s.is_finite()));
    assert!(out.iter().any(|s| s.abs() > 0.001));
    chim_set_lyrics(v, 0);
    chim_free(v);
    chim_lyrics_free(l);
    out
}
fn main() {
    let dir = std::env::args().nth(1).expect("output directory");
    // Each candidate changes one existing source control; no anatomy or planner changes.
    let variants = [
        ("baseline", [0.5, 0.25, 0.15]),
        ("tension", [0.3, 0.25, 0.15]),
        ("brightness", [0.5, 0.55, 0.15]),
        ("breathiness", [0.5, 0.25, 0.35]),
    ];
    for (name, source) in variants {
        let mut out = vec![0f32; 12000];
        for vowel in [2.0, 0.0, 4.0] {
            out.extend(render(vowel, None, &[57, 62, 67], 0.9, source));
            out.extend(vec![0f32; 12000]);
        }
        out.extend(render(2.0, Some("we sing in the warm sun"), &[60, 62, 64, 67, 64, 60], 0.65, source));
        out.extend(vec![0f32; 24000]);
        out.extend(render(2.0, Some("pop tea church blue skies shine"), &[60, 62, 64, 65, 67, 60], 0.7, source));
        out.extend(vec![0f32; 12000]);
        let mut f = std::fs::File::create(format!("{dir}/{name}.f32")).unwrap();
        for x in &out { f.write_all(&x.to_le_bytes()).unwrap(); }
        println!("{name}: {} samples, finite, non-silent, voice alive", out.len());
    }
}
