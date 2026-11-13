//! Compare the demo's full-phrase route with the current wrapper, using a shared score.
//! Output mono f32 at 48 kHz. See audition/phrase-vs-wrapper-01/README.md.
use chimera::anatomy::human::{BodyMix, BODY_FEMALE, NEUTRAL};
use chimera::phonetics::english::EnglishG2P;
use chimera::phonetics::sing::{plan_song, SongOptions, SungNote};
use chimera::phonetics::syllables::lyrics_to_syllables;
use chimera::voice::human_voice::{HumanVoice, SourcePatch};
use chimera_rack::*;
use std::{ffi::CString, io::{Read, Write}};
const FS: f64 = 48000.0;
const BPM: f64 = 96.0;
const START: f64 = 0.6;
const TEXT: &str = "we sing in the warm sun pop tea church blue skies shine";
const MIDI: [i32; 12] = [60, 62, 64, 67, 64, 60, 60, 62, 64, 65, 67, 60];
const BEATS: [f64; 12] = [1.0, 1.0, 0.5, 0.5, 1.0, 2.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0];
fn hz(m: i32) -> f64 { 440.0 * 2f64.powf((m as f64 - 69.0) / 12.0) }
fn write(dir: &str, name: &str, out: &[f32]) {
    assert!(out.iter().all(|s| s.is_finite()), "{name}: nonfinite output");
    assert!(out.iter().any(|s| s.abs() > 0.001), "{name}: silent output");
    let mut f = std::fs::File::create(format!("{dir}/{name}.f32")).unwrap();
    for x in out { f.write_all(&x.to_le_bytes()).unwrap(); }
    println!("{name}: {} samples, finite, non-silent; raw peak {:.4}", out.len(), out.iter().fold(0f32, |m,x|m.max(x.abs())));
}
fn main() {
    let dir = std::env::args().nth(1).expect("output directory");
    let mut dict = String::new();
    flate2::read::GzDecoder::new(&include_bytes!("../../../engine/public/data/cmudict.txt.gz")[..]).read_to_string(&mut dict).unwrap();
    let mut g = EnglishG2P::new(); g.load_dict(&dict);
    let syls = lyrics_to_syllables(TEXT, &g);
    assert_eq!(syls.len(), MIDI.len());
    let notes: Vec<_> = syls.into_iter().enumerate().map(|(i,syl)| SungNote { midi: MIDI[i] as f64, beats: BEATS[i], syl }).collect();
    let mut starts = Vec::new(); let mut end = START;
    for beats in BEATS { starts.push(end); end += beats * 60.0 / BPM; }
    let total = ((end + 1.5) * FS).ceil() as usize / 16 * 16;
    for depth in [0.0, 35.0] {
        let plan = plan_song(&notes, &SongOptions { body: BodyMix::FEMALE, bpm: BPM, t0: START, vibrato: depth });
        assert!(plan.end + 0.3 < total as f64 / FS);
        // Match render_notes' voice construction and whole-phrase scheduling.
        let mut voice = HumanVoice::new(FS, BODY_FEMALE, 2);
        voice.set_source(&SourcePatch {
            f0: Some(hz(MIDI[0])), pressure: Some(9.0), tension: Some(0.55),
            brightness: Some(0.25), breathiness: Some(0.15), roughness: Some(0.1),
            glide: Some(0.06), vibrato_rate: Some(5.5), vibrato_depth: Some(0.0),
            fry: Some(0.0), voiced: Some(true), ..Default::default()
        });
        voice.schedule(&plan.gestures);
        let mut out = vec![0f32; total];
        for block in out.chunks_mut(16) { voice.render(block); }
        write(&dir, &format!("phrase_{}", depth as i32), &out);
        // Match Phona's control cadence. Gates last 80% of each written note.
        let v = chim_new(FS, 1.0, 0.0); assert!(!v.is_null());
        let c = CString::new(TEXT).unwrap();
        let l = chim_lyrics_new(c.as_ptr(), 1.0, std::ptr::null());
        assert_eq!(chim_lyrics_len(l), MIDI.len());
        chim_set_lyrics(v, 1);
        let mut held: Option<usize> = None;
        for (b, block) in out.chunks_mut(16).enumerate() {
            let t = (b * 16) as f64 / FS;
            let i = starts.iter().rposition(|start| t >= *start).unwrap_or(0);
            let on = t >= START && t < starts[i] + BEATS[i] * 60.0 / BPM * 0.8;
            let want = if on {Some(i)} else {None};
            if want != held {
                if held.is_some() { chim_note_off(v); }
                if let Some(k) = want { chim_note_on(v, l, k); }
                held = want;
            }
            let p = ChimParams {
                f0: hz(MIDI[i]), pressure: 9.0, tension: 0.55, brightness: 0.25,
                breathiness: 0.15, roughness: 0.1, glide: 0.06, vib_depth: depth, vib_rate: 5.5,
                fry: 0.0, vowel: 2.0, velum: NEUTRAL.velum, larynx: 0.0, ventricular: 0.0,
                gate: if on {1.0} else {0.0}
            };
            if b == 0 {
                // Discard voice-construction transients with the gate closed before the score.
                let mut discard = [0f32; 16];
                for _ in 0..1500 {
                    chim_set(v, &p);
                    chim_render(v, discard.as_mut_ptr(), discard.len());
                }
            }
            chim_set(v, &p); chim_render(v, block.as_mut_ptr(), block.len());
        }
        assert_eq!(chim_is_dead(v), 0);
        chim_set_lyrics(v, 0); chim_free(v); chim_lyrics_free(l);
        write(&dir, &format!("wrapper_{}", depth as i32), &out);
    }
}
