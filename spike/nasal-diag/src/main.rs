use chimera::anatomy::human::{ArticKey, ArticPatch, BodyMix};
use chimera::engine::Engine;
use chimera::phonetics::planner::vowel_artic;
use chimera::voice::human_voice::SourcePatch;
fn main() {
    for (vw, vel) in [("ɑ", -0.15), ("ɑ", 1.4), ("o", -0.15), ("o", 0.6), ("o", 1.0), ("u", 1.0)] {
        let body = BodyMix::MALE;
        let mut e = Engine::new(48000.0);
        let a = vowel_artic(vw, body).unwrap().with(ArticKey::Velum, vel);
        e.set_artic(&a, Some(true));
        e.set_source(&SourcePatch::f0(130.8));
        e.set_gate(1.0);
        let mut out = vec![0f32; 24000];
        e.render(&mut out);
        let v = e.voice();
        let f = v.airway.frame.as_ref().unwrap();
        let min_area = f.grid.iter().map(|g| g.area).fold(f64::MAX, f64::min);
        let rms = (out[12000..].iter().map(|x| (*x as f64).powi(2)).sum::<f64>() / 12000.0).sqrt();
        println!("{vw} velum {vel:5.2}: rms {:7.1} dB  velum_port {:.2}  lip_area {:.2}  min grid area {:.3}  split {}  n_grid {}  glottis amp {:.3}",
            20.0 * rms.log10(), f.velum_port, f.lip_area, min_area, f.split_index, f.grid.len(), v.glottis.params.amplitude);
        let areas: Vec<String> = f.grid.iter().map(|g| format!("{:.2}", g.area)).collect();
        println!("   areas: {}", areas.join(" "));
    }
}
