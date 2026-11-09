use chimera_rack::*;
fn level(female: f64, vowel: f64, velum: f64) -> f64 { level_l(female, vowel, velum, 0.0) }
fn level_l(female: f64, vowel: f64, velum: f64, larynx: f64) -> f64 {
    let v = chim_new(48000.0, female, 0.0);
    let p = ChimParams { f0: if female > 0.0 { 220.0 } else { 130.8 }, pressure: 8.0, breathiness: 0.15, tension: 0.5, brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel, gate: 1.0, glide: 0.06, fry: 0.0, velum, larynx, ventricular: 0.0 };
    let mut out = vec![0f32; 48000];
    for k in 0..out.len() { if k % 16 == 0 { chim_set(v, &p); } chim_render(v, &mut out[k], 1); }
    chim_free(v);
    let t = &out[24000..];
    20.0 * ((t.iter().map(|x| (*x as f64).powi(2)).sum::<f64>() / t.len() as f64).sqrt()).log10()
}
fn main() {
    for fem in [0.0, 1.0] { let v = chim_new(48000.0, fem, 0.0); let caps: Vec<String> = (0..=16).map(|i| format!("{:.2}", chim_velum_cap(v, i as f64 / 4.0))).collect(); println!("cap {}: {}", fem, caps.join(" ")); chim_free(v); }
    print!("{:>8}", "vowel");
    for n in [0.0, 0.25, 0.5, 0.75, 1.0] { print!("{:>11}", format!("nasal {:.0}%", n * 100.0)); }
    println!();
    for fem in [0.0, 1.0] {
        println!("{}", if fem > 0.0 { "female" } else { "male" });
        for i in 0..=8 {
            let vw = 2.0 + 0.25 * i as f64;
            print!("{vw:>8.2}");
            for n in [0.0, 0.25, 0.5, 0.75, 1.0] { print!("{:>11.1}", level(fem, vw, -0.25 + 1.65 * n)); }
            println!();
        }
    }
    let mut worst = 0.0f64;
    for fem in [0.0, 1.0] { for i in 0..=16 { for lx in [-1.6, 1.6] { for fold in [0.0] { let _ = fold;
        let l = level_l(fem, i as f64 / 4.0, 1.4, lx); worst = worst.min(l); if l < -60.0 { println!("SILENT fem {fem} vowel {} larynx {lx}: {l:.1}", i as f64 / 4.0); } } } } }
    println!("worst level with larynx extremes + nasal 100%: {worst:.1} dB");
}
