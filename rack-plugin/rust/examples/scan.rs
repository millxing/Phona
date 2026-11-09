// Stability scan: push source params past their nominal 0..1 and report blow-ups.
use chimera_rack::*;
fn run(female: f64, f0: f64, set: &dyn Fn(&mut ChimParams)) -> (f64, f64, bool) {
    let v = chim_new(48000.0, female, 0.0);
    let mut p = ChimParams { f0, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0, gate: 1.0, glide: 0.06, fry: 0.0, velum: -0.15, larynx: 0.0, ventricular: 0.0 };
    set(&mut p);
    let mut out = vec![0f32; 96000];
    for k in 0..out.len() { if k % 16 == 0 { p.vowel = 4.0 * k as f64 / 96000.0; chim_set(v, &p); } chim_render(v, &mut out[k], 1); }
    let dead = chim_is_dead(v) != 0; chim_free(v);
    let t = &out[24000..];
    let rms = 20.0 * ((t.iter().map(|x| (*x as f64).powi(2)).sum::<f64>() / t.len() as f64).sqrt()).log10();
    let peak = t.iter().fold(0f32, |m, x| m.max(x.abs())) as f64;
    (rms, peak, dead)
}
fn main() {
    let cases: Vec<(&str, Box<dyn Fn(&mut ChimParams)>)> = vec![
        ("velum -0.25", Box::new(|p| p.velum = -0.25)), ("velum 0.5", Box::new(|p| p.velum = 0.5)), ("velum 1.4", Box::new(|p| p.velum = 1.4)),
        ("larynx -1.6", Box::new(|p| p.larynx = -1.6)), ("larynx +1.6", Box::new(|p| p.larynx = 1.6)),
        ("folds 1", Box::new(|p| p.ventricular = 1.0)),
        ("all extreme", Box::new(|p| { p.velum = 1.4; p.larynx = 1.6; p.ventricular = 1.0; p.tension = 1.5; p.brightness = 1.5; p.roughness = 2.0; p.breathiness = 1.5; p.fry = 1.0; })),
    ];
    for (name, f) in &cases {
        for (fem, f0) in [(0.0, 130.8), (1.0, 293.7)] {
            let (r, pk, d) = run(fem, f0, f.as_ref());
            println!("{name:12} {} rms {r:6.1} dB peak {pk:.2}{}", if fem > 0.0 { "F" } else { "M" }, if d { "  DEAD/NaN" } else { "" });
        }
    }
}
