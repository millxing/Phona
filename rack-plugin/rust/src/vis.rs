//! Snapshots of a voice's anatomy and sound for the Phona View expander (see `ChimVis` in
//! chimera_rack.h). `chim_vis_capture` copies the engine's current midsagittal frame and its
//! telemetry into a fixed C struct: no allocation, so the audio thread can call it.
use super::ChimVoice;
use chimera::anatomy::geom::Vec2;
use chimera::anatomy::human::{ArticKey, BodyMix, HumanFrame, NEUTRAL};
use chimera::phonetics::planner::vowel_artic;
use chimera::voice::human_airway::HumanAirway;
use chimera::voice::human_voice::VoiceTelemetry;
use std::panic::{catch_unwind, AssertUnwindSafe};

pub const VIS_PTS: usize = 96;
pub const VIS_GRID: usize = 64;
pub const VIS_NODES: usize = 64;
pub const VIS_SCOPE: usize = 128;
pub const VIS_LINES: usize = 11;
pub const VIS_NOISE: usize = 6;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ChimVisLine {
    pub n: i32,
    pub x: [f32; VIS_PTS],
    pub y: [f32; VIS_PTS],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ChimVis {
    pub valid: i32,
    pub scale: f32,
    pub pharynx: f32,
    pub jaw: f32,
    pub larynx: f32,
    pub glottis: [f32; 2],
    pub tongue_center: [f32; 2],
    pub tongue_radius: f32,
    pub tip: [f32; 2],
    pub lip_s: f32,
    pub velum_port: f32,
    pub lip_area: f32,
    pub line: [ChimVisLine; VIS_LINES],
    pub n_grid: i32,
    pub g_in_x: [f32; VIS_GRID],
    pub g_in_y: [f32; VIS_GRID],
    pub g_out_x: [f32; VIS_GRID],
    pub g_out_y: [f32; VIS_GRID],
    pub g_s: [f32; VIS_GRID],
    pub g_area: [f32; VIS_GRID],
    pub n_nodes: i32,
    pub node_s: [f32; VIS_NODES],
    pub node_area: [f32; VIS_NODES],
    pub pressure: [f32; VIS_NODES],
    pub flow: [f32; VIS_NODES],
    pub n_noise: i32,
    pub noise_level: [f32; VIS_NOISE],
    pub noise_s: [f32; VIS_NOISE],
    pub f0: f32,
    pub voicing: f32,
    pub p_lung: f32,
    pub out_rms: f32,
    pub gate: f32,
    pub abduction: f32,
    pub n_scope: i32,
    pub glottal_area: [f32; VIS_SCOPE],
    pub glottal_flow: [f32; VIS_SCOPE],
    pub n_ph: i32,
    pub ph_len: f32,
    pub or_len: f32,
    pub phoneme: [u8; 24],
}

fn fill_line(dst: &mut ChimVisLine, src: &[Vec2]) {
    let n = src.len().min(VIS_PTS);
    for (i, p) in src.iter().take(n).enumerate() {
        dst.x[i] = p.x as f32;
        dst.y[i] = p.y as f32;
    }
    dst.n = n as i32;
}

/// The frame's outlines and grid (the anatomy alone).
fn fill_frame(out: &mut ChimVis, f: &HumanFrame) {
    let lines: [&[Vec2]; VIS_LINES] = [&f.outer, &f.inner, &f.tongue, &f.velum, &f.upper_lip, &f.lower_lip,
        &f.upper_teeth, &f.lower_teeth, &f.jaw_bone, &f.epiglottis, &f.nasal_cavity];
    for (d, s) in out.line.iter_mut().zip(lines) {
        fill_line(d, s);
    }
    let n = f.grid.len().min(VIS_GRID);
    for (i, g) in f.grid.iter().take(n).enumerate() {
        out.g_in_x[i] = g.inner.x as f32;
        out.g_in_y[i] = g.inner.y as f32;
        out.g_out_x[i] = g.outer.x as f32;
        out.g_out_y[i] = g.outer.y as f32;
        out.g_s[i] = g.s as f32;
        out.g_area[i] = g.area as f32;
    }
    out.n_grid = n as i32;
    out.glottis = [f.glottis.x as f32, f.glottis.y as f32];
    out.tongue_center = [f.tongue_center.x as f32, f.tongue_center.y as f32];
    out.tongue_radius = f.tongue_radius as f32;
    out.tip = [f.tip.x as f32, f.tip.y as f32];
    out.lip_s = f.lip_s as f32;
    out.velum_port = f.velum_port as f32;
    out.lip_area = f.lip_area as f32;
}

/// The last gesture label that names a speech sound (the planner also labels helper gestures
/// with words: "velum", "aspiration", "voiceless", "voiced frication").
fn phoneme_of(t: &VoiceTelemetry, out: &mut [u8; 24]) {
    out.fill(0);
    let mut pick: Option<&str> = None;
    for l in t.active.iter() {
        let s = l.as_str();
        let word = s.len() >= 4 && s.bytes().all(|b| b.is_ascii_lowercase() || b == b' ');
        if !s.is_empty() && !word {
            pick = Some(s);
        }
    }
    if let Some(s) = pick {
        let b = s.as_bytes();
        let n = b.len().min(23);
        out[..n].copy_from_slice(&b[..n]);
    }
}

/// Copy voice `v`'s anatomy and sound into `out`. Real-time safe (after the warm-up in
/// `chim_new`, which fills `ChimVoice::vis_tele` once). Returns 1 on success, 0 when there is
/// nothing to show yet (no frame, or a dead or null voice); `out->valid` says the same.
#[no_mangle]
pub extern "C" fn chim_vis_capture(v: *mut ChimVoice, out: *mut ChimVis) -> i32 {
    if out.is_null() {
        return 0;
    }
    let out = unsafe { &mut *out };
    out.valid = 0;
    if v.is_null() {
        return 0;
    }
    let v = unsafe { &mut *v };
    if v.dead {
        return 0;
    }
    let r = catch_unwind(AssertUnwindSafe(|| {
        let voice = v.engine.voice_mut();
        voice.telemetry(&mut v.vis_tele);
        let voice = v.engine.voice();
        let a = &voice.airway;
        let Some(f) = a.frame.as_ref() else { return false };
        fill_frame(out, f);
        let t = &v.vis_tele;
        out.scale = a.body.scale as f32;
        out.pharynx = a.body.pharynx as f32;
        out.jaw = t.artic.get(ArticKey::Jaw.index()).copied().unwrap_or(0.0);
        out.larynx = t.artic.get(ArticKey::Larynx.index()).copied().unwrap_or(0.0);
        // tract nodes: pharynx then oral, each at the middle of its section
        let (nph, nor) = (t.ph_areas.len(), t.or_areas.len());
        let n = (nph + nor).min(VIS_NODES).min(t.pressure_rms.len()).min(t.flow.len());
        for i in 0..n {
            let (s, area) = if i < nph {
                ((i as f64 + 0.5) / nph as f64 * t.ph_len, t.ph_areas[i])
            } else {
                let j = i - nph;
                (t.ph_len + (j as f64 + 0.5) / nor as f64 * t.or_len, t.or_areas[j])
            };
            out.node_s[i] = s as f32;
            out.node_area[i] = area;
            out.pressure[i] = t.pressure_rms[i];
            out.flow[i] = t.flow[i];
        }
        out.n_nodes = n as i32;
        // every turbulence source and where it sits (cm from the glottis). The web view shows the
        // loudest only; for [s] that is often the open glottis, which hid the hiss at the teeth.
        let (ph, or) = (a.pharynx, a.oral);
        let mut k = 0;
        for src in a.net.noise_sources() {
            if k == VIS_NOISE {
                break;
            }
            let b = src.src;
            let s = if b >= ph.branch_base && b + 1 < ph.branch_base + ph.n {
                (b - ph.branch_base + 1) as f64 / ph.n as f64 * t.ph_len
            } else if b >= or.branch_base && b + 1 < or.branch_base + or.n {
                t.ph_len + (b - or.branch_base + 1) as f64 / or.n as f64 * t.or_len
            } else if b == a.glottis_port {
                0.2
            } else {
                continue;
            };
            out.noise_s[k] = s as f32;
            out.noise_level[k] = src.level as f32;
            k += 1;
        }
        out.n_noise = k as i32;
        out.f0 = t.f0 as f32;
        out.voicing = t.voicing as f32;
        out.p_lung = t.p_lung as f32;
        out.out_rms = t.out_rms as f32;
        out.gate = t.gate as f32;
        out.abduction = voice.glottis.params.abduction as f32;
        let ns = t.glottal_area.len();
        let m = VIS_SCOPE.min(ns);
        for i in 0..m {
            out.glottal_area[i] = t.glottal_area[i * ns / m];
            out.glottal_flow[i] = t.glottal_flow.get(i * ns / m).copied().unwrap_or(0.0);
        }
        out.n_ph = nph.min(VIS_NODES) as i32;
        out.ph_len = t.ph_len as f32;
        out.or_len = t.or_len as f32;
        out.n_scope = m as i32;
        phoneme_of(t, &mut out.phoneme);
        true
    }));
    if matches!(r, Ok(true)) {
        out.valid = 1;
        1
    } else {
        0
    }
}

/// A still picture of a resting voice (the vowel [ɑ], mouth relaxed), for the module browser and
/// for a View with no Phona next to it. Allocates: UI or worker thread only.
#[no_mangle]
pub extern "C" fn chim_vis_rest(female: f64, out: *mut ChimVis) -> i32 {
    if out.is_null() {
        return 0;
    }
    let out = unsafe { &mut *out };
    out.valid = 0;
    let r = catch_unwind(AssertUnwindSafe(|| {
        let body = BodyMix::new(if female.is_finite() { female.clamp(0.0, 1.0) } else { 0.0 }, 0.0);
        let mut airway = HumanAirway::new(48000.0, body.body());
        airway.tract.velum_junction = true;
        let mut a = NEUTRAL;
        if let Some(p) = vowel_artic("ɑ", body) {
            for k in ArticKey::ALL {
                if let Some(x) = p.get(k) {
                    *a.get_mut(k) = x;
                }
            }
        }
        *a.get_mut(ArticKey::Velum) = -0.15;
        let f = airway.apply(&a, 0.0).clone();
        fill_frame(out, &f);
        out.scale = airway.body.scale as f32;
        out.pharynx = airway.body.pharynx as f32;
        out.jaw = a.get(ArticKey::Jaw) as f32;
        out.larynx = a.get(ArticKey::Larynx) as f32;
        out.n_nodes = 0;
        out.n_noise = 0;
        out.n_scope = 0;
        out.abduction = 0.0;
        out.phoneme = [0; 24];
    }));
    if r.is_ok() {
        out.valid = 1;
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn zeroed() -> Box<ChimVis> {
        // all-zero is a valid ChimVis (plain numbers and arrays)
        unsafe { Box::new(std::mem::zeroed()) }
    }

    #[test]
    fn capture_fills_the_snapshot() {
        let v = crate::chim_new(48000.0, 0.0, 0.0);
        let p = crate::ChimParams { f0: 130.0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
        let mut buf = [0f32; 512];
        for _ in 0..40 {
            crate::chim_set(v, &p);
            crate::chim_render(v, buf.as_mut_ptr(), buf.len());
        }
        let mut s = zeroed();
        assert_eq!(chim_vis_capture(v, &mut *s), 1);
        for (i, l) in s.line.iter().enumerate() {
            assert!(l.n >= 2, "outline {i} is empty");
            assert!((l.n as usize) < VIS_PTS, "outline {i} may be cut off: {}", l.n);
        }
        assert!(s.n_grid > 10 && (s.n_grid as usize) < VIS_GRID);
        assert!(s.n_nodes > 10 && s.pressure[..s.n_nodes as usize].iter().any(|p| *p > 0.0));
        assert!(s.voicing > 0.2 && s.f0 > 100.0 && s.n_scope == VIS_SCOPE as i32);
        crate::chim_free(v);
        let mut r = zeroed();
        assert_eq!(chim_vis_rest(1.0, &mut *r), 1);
        assert!(r.line[2].n > 10 && r.scale < 1.0);
        assert_eq!(chim_vis_capture(std::ptr::null_mut(), &mut *r), 0);
        assert_eq!(r.valid, 0);
    }
}
