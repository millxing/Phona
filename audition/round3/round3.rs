use chimera::{anatomy::human::{ArticKey,ArticPatch,BodyMix},control::{articulators::artic_spec,score::ControlKey},engine::Engine,phonetics::{Segment,Syllable,planner::vowel_artic,sing::{plan_song,midi_to_hz,SongOptions,SungNote}},voice::human_voice::{HumanVoice,SourcePatch}};
use std::{fs,path::Path,time::Instant};
use serde_json::{json,Value};
const FS:f64=48000.; const BLOCK:usize=48;
fn save(dir:&Path,name:&str,out:&[f32]) {fs::write(dir.join(format!("{name}.f32")),out.iter().flat_map(|x|x.to_le_bytes()).collect::<Vec<_>>()).unwrap();}
fn patch(name:&str,x:f64)->SourcePatch {serde_json::from_value(json!({name:x})).unwrap()}
fn quality(body:BodyMix,kind:&str,os:usize)->(Vec<f32>,f64) {
 let mut v=HumanVoice::new(FS,body.body(),os);
 let female=body==BodyMix::FEMALE;
 let base=if female {57.} else {43.};
 v.set_source(&SourcePatch::f0(if kind=="phrase" {midi_to_hz(base)} else if female {294.} else {147.}));
 let a=vowel_artic("i",body).unwrap();let b=vowel_artic("u",body).unwrap();
 if kind=="phrase" {
  let notes:Vec<_>=[0.,2.,4.,7.,0.].iter().enumerate().map(|(i,d)|SungNote{midi:base+d,beats:if i==4{2.}else{1.},syl:Syllable{text:"ah".into(),onset:vec![],nucleus:Segment::new("ɑ"),coda:vec![],melisma:None,word_end:true,phrase_end:i==4}}).collect();
  let mut plan=plan_song(&notes,&SongOptions{body,bpm:110.,t0:0.3,vibrato:0.});
  for g in &mut plan.gestures {g.targets.remove(ControlKey::VIBRATO);}
  v.schedule(&plan.gestures);
 }else{v.set_articulation(&a,None);v.set_gate(1.);}
 let mut out=vec![0.;192000];let start=Instant::now();
 for (i,block) in out.chunks_mut(BLOCK).enumerate(){
  if kind=="vowel-i-u" {let t=(i*BLOCK) as f64/(out_len()-1) as f64;let mut p=ArticPatch::default();for k in ArticKey::ALL {if let (Some(x),Some(y))=(a.get(k),b.get(k)){p.set(k,x+(y-x)*t);}}v.set_articulation(&p,None);}
  v.render(block);
 }
 (out,start.elapsed().as_secs_f64())
}
fn out_len()->usize{192000}
fn main(){
 chimera::init();let args:Vec<_>=std::env::args().collect();let dir=Path::new(&args[1]);fs::create_dir_all(dir).unwrap();
 let keys=[ArticKey::Velum,ArticKey::Larynx,ArticKey::Epilarynx,ArticKey::Ventricular,ArticKey::LipProt,ArticKey::LipWidth,ArticKey::TtCurl,ArticKey::Root,ArticKey::Lateral,ArticKey::Dorsum];
 let mut meta:Vec<Value>=vec![];
 for (gender,body,f0) in [("male",BodyMix::MALE,147.),("female",BodyMix::FEMALE,294.)]{
  let mut controls:Vec<(&str,Option<ArticKey>,f64,f64)>=vec![("pressure",None,2.,16.),("breathiness",None,0.,1.),("tension",None,0.,1.),("brightness",None,0.,1.),("roughness",None,0.,1.),("fry",None,0.,1.)];
  for k in keys {let s=artic_spec(k);controls.push((k.name(),Some(k),s.min,s.max));}
  controls.push(("whisper",None,0.,0.));controls.push(("baseline",None,0.,0.));
  for (name,key,lo,hi) in controls{
   for mode in if name=="whisper"||name=="baseline" {vec!["static"]}else{vec!["sweep","min","max"]}{
    let mut e=Engine::new(FS);e.set_body(body);e.set_source(&SourcePatch::f0(f0));e.set_artic(&vowel_artic("ɑ",body).unwrap(),None);
    if name=="whisper" {e.set_source(&SourcePatch{voiced:Some(false),..Default::default()});}
    e.set_gate(1.);let n=if mode=="sweep"{288000}else{96000};let mut out=vec![0.;n];
    let mut target=0.;let mut actual=0.;
    for (i,block) in out.chunks_mut(BLOCK).enumerate(){
     let phase=(i*BLOCK)as f64/(n-1)as f64;let x=if mode=="min"{lo}else if mode=="max"{hi}else{lo+(hi-lo)*(1.-(2.*phase-1.).abs())};
     if let Some(k)=key {e.set_artic(&ArticPatch::default().with(k,x),None);}else if name!="whisper"&&name!="baseline"{e.set_source(&patch(name,x));}
     e.render(block);
     if let Some(k)=key{target=e.voice().artic.target.get(k);actual=e.voice().artic.x.get(k);}
    }
    let file=format!("{gender}-{name}-{mode}");save(dir,&file,&out);
    meta.push(json!({"name":file,"gender":gender,"control":name,"mode":mode,"min":lo,"max":hi,"f0":f0,"samples":n,"target_end":target,"actual_end":actual}));
   }
  }
 }
 fs::write(dir.join("manifest.json"),serde_json::to_string_pretty(&meta).unwrap()).unwrap();
 let mut timings=vec![];
 for (gender,body) in [("female",BodyMix::FEMALE),("male",BodyMix::MALE)]{for kind in ["phrase","vowel-i-u"]{
  for os in [1,2]{let _=quality(body,kind,os);}
  for rep in 0..5 {for os in if rep%2==0{[2,1]}else{[1,2]}{
   let(out,seconds)=quality(body,kind,os);let name=format!("quality-{gender}-{kind}-os{os}");if rep==0{save(dir,&name,&out);}
   timings.push(json!({"name":name,"os":os,"rep":rep,"seconds":seconds,"core_percent":100.*seconds/4.,"nonfinite":out.iter().filter(|x|!x.is_finite()).count(),"first_nonfinite_seconds":out.iter().position(|x|!x.is_finite()).map(|i|i as f64/FS)}));
  }}
 }}
 fs::write(dir.join("timings.json"),serde_json::to_string_pretty(&timings).unwrap()).unwrap();
 println!("Rendered {} control/reference files and 8 quality files",meta.len());
}
