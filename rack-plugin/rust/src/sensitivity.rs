//! Offline parameter sensitivity audit. Test-only: private wrapper access stays out of the
//! shipped library. Renders one file per changed value into PHONA_SENS_DIR, with a manifest;
//! `audition/sensitivity-01/analyze.py` measures how far each one is from its baseline.
use super::*;
use chimera::control::beat_player::BeatPlayer;
use std::{ffi::CString, io::Write, path::PathBuf};
const FS: usize = 48000;

struct Out {
    dir: PathBuf,
    manifest: std::fs::File,
    n: usize,
}
impl Out {
    fn new() -> Out {
        let dir = PathBuf::from(std::env::var("PHONA_SENS_DIR").expect("PHONA_SENS_DIR"));
        std::fs::create_dir_all(&dir).unwrap();
        let tag = std::thread::current().name().unwrap_or("x").replace("::", "_");
        let manifest = std::fs::File::create(dir.join(format!("manifest_{tag}.tsv"))).unwrap();
        Out { dir, manifest, n: 0 }
    }
    /// `set` = the group of renders that share a baseline; `name` "baseline" marks it.
    fn save(&mut self, set: &str, group: &str, name: &str, x: &[f32]) {
        assert!(x.iter().all(|v| v.is_finite()), "{set} {name}: nonfinite");
        let file = format!("{set}__{:04}.f32", self.n);
        self.n += 1;
        let mut f = std::fs::File::create(self.dir.join(&file)).unwrap();
        let mut bytes = Vec::with_capacity(x.len() * 4);
        for v in x {
            bytes.extend_from_slice(&v.to_le_bytes());
        }
        f.write_all(&bytes).unwrap();
        writeln!(self.manifest, "{file}\t{set}\t{group}\t{name}").unwrap();
    }
}

type Edit = Box<dyn Fn(&mut Vec<Gesture>)>;
type Setup = Box<dyn Fn(&mut ChimVoice)>;

/// One hit through the wrapper's voice, scheduled as `chim_beat_hit` does (scale-1 custom
/// scheduling was checked bit-identical to it in beat_compare). Returns 0.7 s from the trigger.
fn beat(id: &str, female: f64, shift: usize, accent: Option<f64>, setup: &dyn Fn(&mut ChimVoice), edit: &dyn Fn(&mut Vec<Gesture>)) -> Vec<f32> {
    let ptr = chim_new(FS as f64, female, 0.0);
    assert!(!ptr.is_null());
    chim_set_beatbox(ptr, 1);
    setup(unsafe { &mut *ptr });
    let body = BodyMix::new(female, 0.0);
    let start = FS / 2 + 47 + shift;
    let mut out = vec![0f32; start + FS * 7 / 10];
    for (k, s) in out.iter_mut().enumerate() {
        if k == start {
            let v = unsafe { &mut *ptr };
            v.engine.stop_speech();
            v.engine.set_gate(0.0);
            let mut gs = Vec::new();
            let lead = BeatPlayer::lead_in_with(id, body, &mut gs);
            gs.clear();
            let mut player = BeatPlayer::new();
            player.body = body;
            let mut step = BeatStep::new(id);
            step.accent = accent;
            player.gestures(&step, 0.0, 0.18, &mut gs);
            edit(&mut gs);
            v.engine.schedule(&gs, lead, false);
            v.beat_muted = false;
        }
        chim_render(ptr, s, 1);
    }
    chim_free(ptr);
    out[start..].to_vec()
}

fn base_gestures(id: &str) -> Vec<Gesture> {
    let mut gs = Vec::new();
    BeatPlayer::new().gestures(&BeatStep::new(id), 0.0, 0.18, &mut gs);
    gs
}

fn gesture_edits(id: &str) -> Vec<(String, String, Edit)> {
    let mut out: Vec<(String, String, Edit)> = Vec::new();
    for (i, g) in base_gestures(id).iter().enumerate() {
        let keys: Vec<&str> = g.targets.iter().map(|(k, _)| k.name()).collect();
        let d = format!("g{i}[{} {:.0}..{:.0}ms s{}]", keys.join("+"), g.t0 * 1e3, g.t1 * 1e3, g.strength);
        let mut add = |what: &str, f: Edit| out.push(("gesture".to_string(), format!("{d} {what}"), f));
        add("removed", Box::new(move |gs| { gs.remove(i); }));
        for s in [0.5, 2.0] {
            add(&format!("strength x{s}"), Box::new(move |gs| gs[i].strength *= s));
            add(&format!("duration x{s}"), Box::new(move |gs| gs[i].t1 = gs[i].t0 + (gs[i].t1 - gs[i].t0) * s));
            add(&format!("speed x{s}"), Box::new(move |gs| gs[i].speed = Some(gs[i].speed.unwrap_or(1.0) * s)));
        }
        for s in [0.25, 4.0] {
            add(&format!("ramp_in x{s}"), Box::new(move |gs| gs[i].ramp_in *= s));
            add(&format!("ramp_out x{s}"), Box::new(move |gs| gs[i].ramp_out *= s));
        }
        for ms in [-15.0, 15.0] {
            add(&format!("shift {ms:+}ms"), Box::new(move |gs| { gs[i].t0 += ms * 1e-3; gs[i].t1 += ms * 1e-3; }));
        }
        for (k, x) in g.targets.iter() {
            for s in [0.85, 1.15] {
                let to = if x.abs() < 1e-6 { (s - 1.0) * 0.7 } else { x * s };
                add(&format!("target {} {x:.3} -> {to:.3}", k.name()), Box::new(move |gs| { gs[i].targets.set(k, to); }));
            }
        }
    }
    out
}

fn setups(female: f64) -> Vec<(String, String, Setup)> {
    let mut out: Vec<(String, String, Setup)> = Vec::new();
    let mut add = |group: &str, name: String, f: Setup| out.push((group.to_string(), name, f));
    for p in [4.0, 16.0] {
        add("source", format!("lung pressure 8 -> {p}"), Box::new(move |v| v.engine.set_source(&SourcePatch { pressure: Some(p), ..Default::default() })));
    }
    for s in [0.0, 0.1, 10.0] {
        add("noise", format!("glottis noise gain x{s}"), Box::new(move |v| v.engine.voice_mut().airway.noise_gain.glottis *= s));
        add("noise", format!("constriction noise gain x{s}"), Box::new(move |v| v.engine.voice_mut().airway.noise_gain.constriction *= s));
        add("noise", format!("teeth noise gain x{s}"), Box::new(move |v| v.engine.voice_mut().airway.noise_gain.teeth *= s));
    }
    add("noise", "all noise gains x0".to_string(), Box::new(|v| {
        let n = &mut v.engine.voice_mut().airway.noise_gain;
        n.glottis = 0.0; n.constriction = 0.0; n.teeth = 0.0;
    }));
    add("output", "engine limiter on".to_string(), Box::new(|v| v.engine.voice_mut().limiter = true));
    add("source", "fold excitation on".to_string(), Box::new(|v| v.engine.voice_mut().fold_scale = 1.0));
    for (key, name, deltas) in [(ArticKey::Jaw, "jaw", [-0.2, 0.3]), (ArticKey::LipAp, "lip opening", [-0.15, 0.3]), (ArticKey::TbY, "tongue height", [-0.3, 0.3])] {
        for d in deltas {
            add("posture", format!("ready posture {name} {d:+}"), Box::new(move |v| {
                let mut a = beat_ready(BodyMix::new(female, 0.0));
                let x = a.get(key).unwrap_or(0.0) + d;
                a.set(key, x);
                v.engine.set_artic(&a, Some(true));
            }));
        }
    }
    out
}

#[test]
#[ignore = "offline sensitivity audit; requires PHONA_SENS_DIR"]
fn sens_beat() {
    let mut o = Out::new();
    let none = |_: &mut ChimVoice| {};
    let same = |_: &mut Vec<Gesture>| {};
    for id in ["t", "Pf", "B"] {
        let set = format!("beat_{id}");
        let base = beat(id, 0.0, 0, None, &none, &same);
        assert!(base.iter().any(|v| v.abs() > 0.001), "{id}: silent baseline");
        assert_eq!(base, beat(id, 0.0, 0, None, &none, &same), "{id}: not deterministic");
        o.save(&set, "baseline", "baseline", &base);
        // the same hit at other trigger offsets: how much two renders of one sound differ
        for shift in [1, 7, 23, 61, 97] {
            o.save(&set, "floor", &format!("trigger +{shift} samples"), &beat(id, 0.0, shift, None, &none, &same));
        }
        for a in [0.5, 1.5] {
            o.save(&set, "accent", &format!("accent {a}"), &beat(id, 0.0, 0, Some(a), &none, &same));
        }
        o.save(&set, "body", "female body", &beat(id, 1.0, 0, None, &none, &same));
        for (group, name, f) in setups(0.0) {
            o.save(&set, &group, &name, &beat(id, 0.0, 0, None, &*f, &same));
        }
        for (group, name, f) in gesture_edits(id) {
            o.save(&set, &group, &name, &beat(id, 0.0, 0, None, &none, &*f));
        }
    }
}

const P0: ChimParams = ChimParams { f0: 130.81, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 0.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };

/// Sung words as the module calls the wrapper: one syllable per gate, every `beat` seconds.
fn sing(text: &str, tune: &[f64; TUNE_N], beat_s: f64, held: f64, shift: usize) -> Vec<f32> {
    let c = CString::new(text).unwrap();
    let l = chim_lyrics_new(c.as_ptr(), 0.0, tune.as_ptr());
    let v = chim_new(FS as f64, 0.0, 0.0);
    chim_set_tune(v, tune.as_ptr());
    chim_set_lyrics(v, 1);
    let syl = chim_lyrics_len(l);
    let n = ((syl as f64 * beat_s + 0.6) * FS as f64) as usize / 16 * 16 + shift;
    let mut out = vec![0f32; n];
    let (mut was, mut k) = (false, 0);
    chim_render(v, out.as_mut_ptr(), shift);
    for b in 0..(n - shift) / 16 {
        let t = (b * 16) as f64 / FS as f64;
        let i = (t / beat_s) as usize;
        let on = i < syl && t - i as f64 * beat_s < held;
        if on && !was { chim_note_on(v, l, k); k += 1; }
        if !on && was { chim_note_off(v); }
        was = on;
        let p = ChimParams { gate: if on { 1.0 } else { 0.0 }, ..P0 };
        chim_set(v, &p);
        chim_render(v, out[shift + b * 16..].as_mut_ptr(), 16);
    }
    assert_eq!(chim_is_dead(v), 0);
    chim_free(v);
    chim_lyrics_free(l);
    out[shift..].to_vec()
}

#[test]
#[ignore = "offline sensitivity audit; requires PHONA_SENS_DIR"]
fn sens_lab() {
    let mut o = Out::new();
    // the ranges and names of PhonaLab.hpp
    let names = ["stopBreath", "stopHiss", "stopCut", "release", "fadeStart", "fadeTime", "exhale", "codaLen", "onsetLen", "onsetHiss", "voiceless", "strength", "minVowel", "nextWait", "level", "limiter", "onsetBreath", "thump"];
    let lo = [0.0, 0.0, 0.0, 0.25, -30.0, 1.0, 0.0, 0.5, 0.5, 0.0, 0.0, 0.25, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0];
    let hi = [1.0, 3.0, 155.0, 4.0, 60.0, 50.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 200.0, 1.5, 3.0, 1.0, 1.0, 1.5];
    let text = "cat stop fish sing bed top";
    for (set, beat_s, held) in [("lab_slow", 0.7, 0.45), ("lab_fast", 0.25, 0.03)] {
        o.save(set, "baseline", "baseline", &sing(text, &TUNE_DEFAULT, beat_s, held, 0));
        for shift in [1, 7, 23] {
            o.save(set, "floor", &format!("start +{shift} samples"), &sing(text, &TUNE_DEFAULT, beat_s, held, shift));
        }
        for i in 0..TUNE_N {
            for x in [lo[i], hi[i]] {
                if x == TUNE_DEFAULT[i] { continue; }
                let mut t = TUNE_DEFAULT;
                t[i] = x;
                o.save(set, "lab", &format!("{} {} -> {x}", names[i], TUNE_DEFAULT[i]), &sing(text, &t, beat_s, held, 0));
            }
        }
    }
}

/// A held vowel through the wrapper with the plain voice controls.
fn vowel(p: &ChimParams, shift: usize) -> Vec<f32> {
    let v = chim_new(FS as f64, 0.0, 0.0);
    let n = FS * 3 / 2;
    let mut out = vec![0f32; n + shift];
    chim_render(v, out.as_mut_ptr(), shift);
    for b in 0..n / 16 {
        let t = (b * 16) as f64 / FS as f64;
        let q = ChimParams { gate: if (0.1..1.1).contains(&t) { 1.0 } else { 0.0 }, ..*p };
        chim_set(v, &q);
        chim_render(v, out[shift + b * 16..].as_mut_ptr(), 16);
    }
    assert_eq!(chim_is_dead(v), 0);
    chim_free(v);
    out[shift..].to_vec()
}

#[test]
#[ignore = "offline sensitivity audit; requires PHONA_SENS_DIR"]
fn sens_knobs() {
    let mut o = Out::new();
    o.save("knobs", "baseline", "baseline", &vowel(&P0, 0));
    for shift in [1, 7, 23] {
        o.save("knobs", "floor", &format!("start +{shift} samples"), &vowel(&P0, shift));
    }
    let mut add = |name: &str, p: ChimParams| o.save("knobs", "knob", name, &vowel(&p, 0));
    for x in [4.0, 16.0] { add(&format!("pressure 8 -> {x}"), ChimParams { pressure: x, ..P0 }); }
    for x in [0.0, 1.0] { add(&format!("breathiness 0.15 -> {x}"), ChimParams { breathiness: x, ..P0 }); }
    for x in [0.0, 1.0] { add(&format!("tension 0.5 -> {x}"), ChimParams { tension: x, ..P0 }); }
    for x in [0.0, 1.0] { add(&format!("brightness 0.25 -> {x}"), ChimParams { brightness: x, ..P0 }); }
    for x in [0.0, 1.0] { add(&format!("roughness 0.1 -> {x}"), ChimParams { roughness: x, ..P0 }); }
    for x in [30.0, 100.0] { add(&format!("vibrato depth 0 -> {x} cents"), ChimParams { vib_depth: x, ..P0 }); }
    for x in [0.75, 1.0] { add(&format!("fry 0 -> {x}"), ChimParams { fry: x, ..P0 }); }
    for x in [0.5, 1.4] { add(&format!("velum -0.15 -> {x}"), ChimParams { velum: x, ..P0 }); }
    for x in [-1.6, 1.6] { add(&format!("larynx 0 -> {x}"), ChimParams { larynx: x, ..P0 }); }
    for x in [0.5, 1.0] { add(&format!("ventricular 0 -> {x}"), ChimParams { ventricular: x, ..P0 }); }
    for x in [0.0, 4.0] { add(&format!("vowel 2 -> {x}"), ChimParams { vowel: x, ..P0 }); }
}

/// A `BeatFit` from `key=value` settings (see `audition/beat-fit-01/fit.py`): ng nc nt = noise
/// gain multipliers; bgl bgh bcl bch btl bth = noise band limits (Hz); press = lung pressure;
/// pj pl pt = ready-posture offsets (jaw, lip opening, tongue height); hp = output high-pass
/// (Hz); gain; shf shg = low shelf (Hz, dB); pkf pkg pkq = peak; lp = low-pass (Hz); visc = x
/// viscous loss; g<i>.t0 = shift (ms); g<i>.dur .ri .ro .str = multipliers; g<i>.spd = speed;
/// g<i>.<control name> = target value. The edits are leaked (test process only).
fn fit_from(kv: &[(String, f64)]) -> BeatFit {
    let mut f = BEAT_PLAIN;
    let mut edits = Vec::new();
    // x<i>.t0 .dur .ri .ro (ms), x<i>.str, x<i>.<control name> = value: an added gesture
    let mut extra: Vec<BeatExtra> = Vec::new();
    for (k, x) in kv {
        let x = *x;
        if let Some((xi, field)) = k.strip_prefix('x').and_then(|r| r.split_once('.')) {
            let i = xi.parse::<usize>().unwrap();
            while extra.len() <= i {
                extra.push(BeatExtra { t0: 0.0, dur: 50.0, ramp_in: 10.0, ramp_out: 20.0, strength: 40.0, key: ControlKey::Artic(ArticKey::LipAp), value: 0.0 });
            }
            let e = &mut extra[i];
            match field {
                "t0" => e.t0 = x, "dur" => e.dur = x, "ri" => e.ramp_in = x, "ro" => e.ramp_out = x, "str" => e.strength = x,
                name => { e.key = ControlKey::from_name(name).unwrap_or_else(|| panic!("unknown key {k}")); e.value = x; }
            }
            continue;
        }
        match k.as_str() {
            "ng" => f.noise[0] = x, "nc" => f.noise[1] = x, "nt" => f.noise[2] = x,
            "bgl" => f.bands[0].0 = x, "bgh" => f.bands[0].1 = x,
            "bcl" => f.bands[1].0 = x, "bch" => f.bands[1].1 = x,
            "btl" => f.bands[2].0 = x, "bth" => f.bands[2].1 = x,
            "press" => f.pressure = x,
            "pj" => f.posture[0] = x, "pl" => f.posture[1] = x, "pt" => f.posture[2] = x,
            "hp" => f.hp = x, "gain" => f.gain = x,
            "shf" => f.shelf.0 = x, "shg" => f.shelf.1 = x,
            "pkf" => f.peak.0 = x, "pkg" => f.peak.1 = x, "pkq" => f.peak.2 = x,
            "lp" => f.lp = x, "visc" => f.viscous = x,
            "hold" => f.hold = x, "tail" => f.tail = x,
            _ => {
                let (gi, field) = k.strip_prefix('g').and_then(|r| r.split_once('.')).unwrap_or_else(|| panic!("unknown key {k}"));
                let what = match field {
                    "t0" => BeatEdit::Shift, "dur" => BeatEdit::Dur, "ri" => BeatEdit::RampIn, "ro" => BeatEdit::RampOut,
                    "str" => BeatEdit::Strength, "spd" => BeatEdit::Speed,
                    name => BeatEdit::Target(ControlKey::from_name(name).unwrap_or_else(|| panic!("unknown key {k}"))),
                };
                edits.push((gi.parse::<usize>().unwrap(), what, x));
            }
        }
    }
    f.edits = Box::leak(edits.into_boxed_slice());
    f.extra = Box::leak(extra.into_boxed_slice());
    f
}

/// Hits through `chim_beat_hit`, as the module plays them: `hits` = (token, sample). `fit`
/// replaces the table entry of `token0` when given. Returns the render from the first hit on.
fn play(hits: &[(i32, usize)], fit: Option<(i32, BeatFit)>, len: usize) -> Vec<f32> {
    let ptr = chim_new(FS as f64, 0.0, 0.0);
    assert!(!ptr.is_null());
    if let Some((token, f)) = fit {
        // as `chim_new` does with the table's values: the mouths of this sound come to rest in its posture
        let v = unsafe { &mut *ptr };
        v.beat_fit[token as usize - 1] = f;
        for b in v.beat_voices.iter_mut().filter(|b| b.sound == token as usize - 1) {
            b.settle(&f, FS as f64);
        }
    }
    chim_set_beatbox(ptr, 1);
    let start = FS / 2 + 47;
    let mut out = vec![0f32; start + len];
    for (k, s) in out.iter_mut().enumerate() {
        for (token, at) in hits {
            if k == start + at { chim_beat_hit(ptr, *token); }
        }
        chim_render(ptr, s, 1);
    }
    assert_eq!(chim_is_dead(ptr), 0);
    chim_free(ptr);
    out[start..].to_vec()
}

fn token_of(id: &str) -> i32 {
    BEAT_IDS.iter().position(|x| *x == id).unwrap_or_else(|| panic!("unknown sound {id}")) as i32 + 1
}

/// One hit of `id` with these settings, through the module's own hit path.
fn fit_render(id: &str, kv: &[(String, f64)]) -> Vec<f32> {
    let token = token_of(id);
    play(&[(token, 0)], Some((token, fit_from(kv))), FS * 6 / 10)
}

/// A mouth that prepares the sound ahead and waits makes the same sound as one that starts at
/// the hit, only sooner: stopping the model and going on changes nothing.
#[test]
fn held_sound_is_the_same_sound() {
    for (k, id) in BEAT_IDS.iter().enumerate() {
        let token = k as i32 + 1;
        let mut late = BEAT_FIT[k];
        late.hold = 0.0;
        let a = play(&[(token, 0)], Some((token, late)), FS * 6 / 10);
        let b = play(&[(token, 0)], Some((token, BEAT_FIT[k])), FS * 6 / 10);
        let shift = (BEAT_FIT[k].hold * FS as f64).ceil() as usize;
        assert!(a.iter().any(|v| v.abs() > 0.01), "{id}: silent");
        // before the hold point the late one makes its (near silent) preparation; from there on they are one sound
        let n = (BEAT_FIT[k].tail * FS as f64) as usize - shift - 64;
        let fade = FS / 500; // each one fades in for 1 ms where it starts to sound
        // the voice clock moves a control step at a time, so the hold point is up to one step early
        let arg = |x: &[f32]| x.iter().enumerate().fold((0, 0f32), |m, (i, v)| if v.abs() > m.1 { (i, v.abs()) } else { m }).0;
        let lag = arg(&a) - arg(&b);
        assert!(lag <= shift && shift - lag <= 64, "{id}: held {} samples before the hit, expected {shift}", lag);
        let n = n.min(a.len() - lag);
        assert!(a[lag + fade..lag + n] == b[fade..n], "{id}: the held sound differs");
    }
}

/// The module's table as `key=value` lines would render it: prints peak levels, and renders
/// a pattern (PHONA_FIT_OUT, f32) to check hits in context.
#[test]
#[ignore = "offline; writes PHONA_FIT_OUT"]
fn table_pattern() {
    // B . t . Pf . t . at 125 ms a step, twice, then B Pf t one second apart
    let step = FS / 8;
    let mut hits: Vec<(i32, usize)> = Vec::new();
    for (i, t) in [1, 0, 3, 0, 2, 0, 3, 0, 1, 3, 2, 3, 1, 1, 2, 3].iter().enumerate() {
        if *t > 0 { hits.push((*t, i * step)); }
    }
    for (i, t) in [1, 2, 3].iter().enumerate() { hits.push((*t, 3 * FS + i * FS)); }
    let x = play(&hits, None, 6 * FS);
    assert!(x.iter().all(|v| v.is_finite()));
    for (i, id) in BEAT_IDS.iter().enumerate() {
        let seg = &x[3 * FS + i * FS..3 * FS + (i + 1) * FS];
        println!("{id}: peak {:.3}", seg.iter().fold(0f32, |m, v| m.max(v.abs())));
    }
    let mut bytes = Vec::with_capacity(x.len() * 4);
    for v in &x { bytes.extend_from_slice(&v.to_le_bytes()); }
    std::fs::write(std::env::var("PHONA_FIT_OUT").expect("PHONA_FIT_OUT"), bytes).unwrap();
}

/// Renders every line of PHONA_FIT_IN ("<id> key=value ...") into PHONA_FIT_OUT: 0.6 s of f32
/// per line, from the trigger. A render that fails or is not finite is all NaN.
#[test]
#[ignore = "offline fit renderer; requires PHONA_FIT_IN and PHONA_FIT_OUT"]
fn fit_batch() {
    let text = std::fs::read_to_string(std::env::var("PHONA_FIT_IN").expect("PHONA_FIT_IN")).unwrap();
    let rows: Vec<(String, Vec<(String, f64)>)> = text.lines().filter(|l| !l.trim().is_empty()).map(|l| {
        let mut it = l.split_whitespace();
        let id = it.next().unwrap().to_string();
        (id, it.map(|t| { let (k, x) = t.split_once('=').unwrap(); (k.to_string(), x.parse().unwrap()) }).collect())
    }).collect();
    let n = FS * 6 / 10;
    let mut all = vec![f32::NAN; rows.len() * n];
    let threads = std::thread::available_parallelism().map_or(4, |x| x.get()).min(rows.len().max(1));
    let per = rows.len().div_ceil(threads).max(1);
    std::thread::scope(|s| {
        for (rs, outs) in rows.chunks(per).zip(all.chunks_mut(per * n)) {
            s.spawn(move || {
                for (r, o) in rs.iter().zip(outs.chunks_mut(n)) {
                    if let Ok(x) = catch_unwind(|| fit_render(&r.0, &r.1)) {
                        if x.iter().all(|v| v.is_finite()) { o.copy_from_slice(&x); }
                    }
                }
            });
        }
    });
    let mut bytes = Vec::with_capacity(all.len() * 4);
    for v in &all { bytes.extend_from_slice(&v.to_le_bytes()); }
    std::fs::write(std::env::var("PHONA_FIT_OUT").expect("PHONA_FIT_OUT"), bytes).unwrap();
}

#[test]
#[ignore = "prints the beat sounds' gestures"]
fn dump_gestures() {
    for id in ["t", "Pf", "B"] {
        println!("{id}: lead {:.1} ms", BeatPlayer::lead_in(id, BodyMix::MALE) * 1e3);
        for (i, g) in base_gestures(id).iter().enumerate() {
            let t: Vec<String> = g.targets.iter().map(|(k, x)| format!("{}={x:.3}", k.name())).collect();
            println!("  g{i} {:7.1}..{:6.1} ms  in {:5.1} out {:5.1}  str {:4.1}  spd {:?}  {}", g.t0 * 1e3, g.t1 * 1e3, g.ramp_in * 1e3, g.ramp_out * 1e3, g.strength, g.speed, t.join(" "));
        }
    }
    let p = beat_ready(BodyMix::MALE);
    println!("ready: jaw {:?} lipAp {:?} tbY {:?}", p.get(ArticKey::Jaw), p.get(ArticKey::LipAp), p.get(ArticKey::TbY));
}

/// Cost of beat mode: the time to build a voice, and the share of real time that hits every
/// 125 ms take (one audio thread), against the plain singing voice.
#[test]
#[ignore = "timing; prints"]
fn bench_beat() {
    let t = std::time::Instant::now();
    let v = chim_new(FS as f64, 0.0, 0.0);
    println!("chim_new: {:.0} ms", t.elapsed().as_secs_f64() * 1e3);
    let mut out = [0f32; 64];
    let p = ChimParams { gate: 1.0, ..P0 };
    let t = std::time::Instant::now();
    for _ in 0..4 * FS / 64 { chim_set(v, &p); chim_render(v, out.as_mut_ptr(), 64); }
    println!("singing voice: {:.1} % of real time", t.elapsed().as_secs_f64() / 4.0 * 100.0);
    chim_set_beatbox(v, 1);
    for _ in 0..FS / 64 { chim_render(v, out.as_mut_ptr(), 64); }
    let t = std::time::Instant::now();
    for _ in 0..4 * FS / 64 { chim_render(v, out.as_mut_ptr(), 64); }
    println!("beat mode, waiting: {:.2} % of real time", t.elapsed().as_secs_f64() / 4.0 * 100.0);
    for (name, every) in [("every 250 ms", FS / 4 / 64), ("every 125 ms", FS / 8 / 64), ("every 62 ms", FS / 16 / 64)] {
        let (t, mut worst) = (std::time::Instant::now(), 0f64);
        for k in 0..4 * FS / 64 {
            if k % every == 0 { chim_beat_hit(v, [1, 3, 2, 3][(k / every) % 4]); }
            let b = std::time::Instant::now();
            chim_render(v, out.as_mut_ptr(), 64);
            worst = worst.max(b.elapsed().as_secs_f64());
        }
        println!("beat mode, a hit {name}: {:.1} % of real time, worst block {:.0} %", t.elapsed().as_secs_f64() / 4.0 * 100.0, worst / (64.0 / FS as f64) * 100.0);
    }
    assert_eq!(chim_is_dead(v), 0);
    chim_free(v);
}

/// The table was fitted on the male body. Where does each sound start on the female one,
/// against the table's hold point?
#[test]
#[ignore = "prints"]
fn female_hold() {
    for female in [0.0, 1.0] {
        for (k, id) in BEAT_IDS.iter().enumerate() {
            let ptr = chim_new(FS as f64, female, 0.0);
            let mut late = BEAT_FIT[k];
            late.hold = 0.0;
            late.tail = 0.6;
            unsafe { &mut *ptr }.beat_fit[k] = late;
            chim_set_beatbox(ptr, 1);
            let mut x = vec![0f32; FS * 6 / 10];
            chim_beat_hit(ptr, k as i32 + 1);
            chim_render(ptr, x.as_mut_ptr(), x.len());
            chim_free(ptr);
            // 1 ms level; onset = 20 % of the peak; the last quiet moment (-40 dB) before it
            let env: Vec<f32> = x.chunks(48).map(|c| (c.iter().map(|v| v * v).sum::<f32>() / 48.0).sqrt()).collect();
            let peak = env.iter().fold(0f32, |m, v| m.max(*v));
            let on = env.iter().position(|v| *v >= 0.2 * peak).unwrap();
            let quiet = env[..on].iter().rposition(|v| *v < 0.01 * peak).unwrap_or(0);
            println!("female {female} {id}: onset {on} ms, last quiet {quiet} ms, table hold {:.0} ms, peak {peak:.2}", BEAT_FIT[k].hold * 1e3);
        }
    }
}
