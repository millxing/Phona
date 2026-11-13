//! Isolated hits and a 120 BPM eighth-note pattern, at the real wrapper control cadence.
//! cargo run --release --example beatbox -- /tmp/phona-beats
use chimera_rack::*;
use std::io::Write;
const FS: usize = 48000;
fn wav(path: &str, x: &[f32]) {
    let mut f = std::fs::File::create(path).unwrap();
    let n = (x.len() * 2) as u32;
    for bytes in [b"RIFF".as_slice(), &(36+n).to_le_bytes(), b"WAVEfmt ", &16u32.to_le_bytes(),
        &1u16.to_le_bytes(), &1u16.to_le_bytes(), &(FS as u32).to_le_bytes(),
        &(FS as u32*2).to_le_bytes(), &2u16.to_le_bytes(), &16u16.to_le_bytes(), b"data", &n.to_le_bytes()] {
        f.write_all(bytes).unwrap();
    }
    for s in x { f.write_all(&((s / 1.6 * 32767.0).clamp(-32767.0,32767.0) as i16).to_le_bytes()).unwrap(); }
}
fn main() {
    let dir = std::env::args().nth(1).expect("output directory");
    std::fs::create_dir_all(&dir).unwrap();
    println!("body,sound,onset_1pct_ms,onset_10pct_ms,onset_50pct_ms,peak_ms,peak");
    for (female, body) in [(0.0,"male"),(1.0,"female")] {
        for (name, tokens) in [("B",vec![1]),("Pf",vec![2]),("t",vec![3]),
            ("pattern",vec![1,3,2,3,1,0,2,3,1,3,2,3,1,3,2,0])] {
            let v = chim_new(FS as f64, female, 0.0);
            assert!(!v.is_null());
            chim_set_beatbox(v,1);
            let p = ChimParams { f0: 130.8, pressure: 8.0, breathiness: 0.15, tension: 0.5,
                brightness: 0.25, roughness: 0.1, vib_depth: 0.0, vib_rate: 5.5, vowel: 2.0,
                gate: 0.0, glide: 0.06, fry: 0.0, velum: 0.0, larynx: 0.0, ventricular: 0.0 };
            let start = FS/2;
            let mut x = vec![0f32; start + tokens.len()*FS/4 + FS];
            for (k,s) in x.iter_mut().enumerate() {
                // Same 48-sample countdown as Rack: edge sample counts as the first sample.
                if k >= start+47 && (k-start-47)%(FS/4)==0 {
                    if let Some(t) = tokens.get((k-start-47)/(FS/4)) { chim_beat_hit(v,*t); }
                }
                if k%16==0 { chim_set(v,&p); }
                chim_render(v,s,1);
            }
            assert_eq!(chim_is_dead(v),0);
            chim_free(v);
            if tokens.len()==1 {
                let hit = &x[start..];
                let (pk, peak) = hit.iter().enumerate().max_by(|a,b| a.1.abs().total_cmp(&b.1.abs())).unwrap();
                let onset = |fraction| hit.iter().position(|s| s.abs()>=peak.abs()*fraction).unwrap() as f64/48.0;
                println!("{body},{name},{:.3},{:.3},{:.3},{:.3},{:.6}",onset(0.01),onset(0.1),onset(0.5),pk as f64/48.0,peak.abs());
            }
            wav(&format!("{dir}/{body}_{name}.wav"), &x);
        }
    }
}
