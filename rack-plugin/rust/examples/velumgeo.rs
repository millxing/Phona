//! Static tract geometry against the velum opening, per vowel: where the tract is narrowest and
//! on which side of the nasal junction (pharynx_length). Arg 1: body 0 male / 1 female.
use chimera::anatomy::human::{ArticKey, BodyMix, NEUTRAL};
use chimera::phonetics::planner::vowel_artic;
use chimera::voice::human_airway::HumanAirway;
fn main() {
    let female: f64 = std::env::args().nth(1).and_then(|s| s.parse().ok()).unwrap_or(0.0);
    let detail = std::env::args().nth(2);
    let body = BodyMix::new(female, 0.0);
    let mut airway = HumanAirway::new(48000.0, body.body());
    airway.tract.velum_junction = std::env::var("STOCK").is_err(); // STOCK=1: the engine's fixed junction
    for vw in ["i", "e", "ɑ", "o", "u"] {
        let patch = vowel_artic(vw, body).unwrap_or_default();
        let mut a = NEUTRAL;
        for k in ArticKey::ALL { if let Some(x) = patch.get(k) { *a.get_mut(k) = x; } }
        println!("vowel {vw}");
        for vel in [-0.25, 0.0, 0.3, 0.6, 0.9, 1.2, 1.4] {
            *a.get_mut(ArticKey::Velum) = vel;
            let f = airway.apply(&a, 0.0);
            let (mut mi, mut ma) = (0, f64::INFINITY);
            for (i, g) in f.grid.iter().enumerate() { if g.s < f.lip_s - 0.5 && g.area < ma { ma = g.area; mi = i; } }
            let g = &f.grid[mi];
            println!("  velum {vel:5.2}: port {:.2} cm2  junction s {:.2}  min area {:.3} at s {:.2} (region {}, {})", f.velum_port, f.pharynx_length, ma, g.s, g.region, if g.s < f.pharynx_length { "BELOW junction" } else { "oral side" });
            if detail.as_deref() == Some(vw) {
                for g in f.grid.iter() { if (g.s - f.pharynx_length).abs() < 3.0 { println!("      s {:5.2} r{} area {:6.3} d {:.3} ray {:.3} outer ({:.2},{:.2}) inner ({:.2},{:.2})", g.s, g.region, g.area, g.d, g.ray, g.outer.x, g.outer.y, g.inner.x, g.inner.y); } }
            }
        }
    }
}
