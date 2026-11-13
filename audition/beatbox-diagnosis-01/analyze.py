"""Reproducible waveform/spectral analysis. Requires existing numpy/scipy and ffmpeg."""
from pathlib import Path
import json, subprocess, wave
import numpy as np
from scipy import signal
P=Path(__file__).resolve().parent
FS=48000
variants=['wrapper','folds','limiter','no_cancel','native_live','native_planned']
def decode(path):
 r=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-ac','1','-ar',str(FS),'-f','f32le','-'],capture_output=True,check=True)
 return np.frombuffer(r.stdout,dtype='<f4').copy()
def load(name):return np.fromfile(P/(name+'.f32'),dtype='<f4')
def crop(x):
 env=np.sqrt(signal.convolve(x.astype(float)**2,np.ones(240)/240,mode='same'))
 ix=np.flatnonzero(env>=env.max()*.2)[0]
 start=int(ix)-960
 # Keep 20ms of preparation, then 380ms. Start-relative alignment is for shape, not latency.
 return np.pad(x,(max(0,-start),19200))[max(0,start):max(0,start)+19200],int(ix)
def features(x):
 x=x.astype(float); en=x*x; cum=np.cumsum(en)/max(en.sum(),1e-30)
 a,b=[np.searchsorted(cum,q) for q in [.05,.95]]
 freqs=np.fft.rfftfreq(len(x),1/FS); power=np.abs(np.fft.rfft(x))**2
 bands=[power[(freqs>=lo)&(freqs<hi)].sum() for lo,hi in zip([0,150,500,2000,8000],[150,500,2000,8000,24001])]
 bands=np.array(bands)/max(sum(bands),1e-30)
 return dict(energy90_ms=float((b-a)/48),centroid_hz=float(np.sum(freqs*power)/max(power.sum(),1e-30)),band_fraction=bands.tolist(),peak=float(np.max(np.abs(x))))
def wav(name,x):
 assert np.isfinite(x).all() and np.max(np.abs(x))<.99
 with wave.open(str(P/(name+'.wav')),'wb') as f:
  f.setparams((1,2,FS,0,'NONE','not compressed'));f.writeframes((x*32767).astype('<i2').tobytes())
refs={'B':['heavy_kick','light_kick'],'Pf':['snare'],'t':['hat']}
all_data={}; metrics={}; hypotheses={}
for body in ['male','female']:
 for case in ['B','Pf','t','pattern','fast']:
  b=load(f'{body}_{case}_wrapper')
  hypotheses[f'{body}_{case}']={}
  for variant in variants[1:]:
   x=load(f'{body}_{case}_{variant}')
   hypotheses[f'{body}_{case}'][variant]=dict(max_abs_difference=float(np.max(np.abs(x-b))),identical=bool(np.array_equal(x,b)))
for sound,refnames in refs.items():
 series={}
 for ref in refnames: series['Reference: '+ref.replace('_',' ')]=decode(P/('ref_'+ref+'.mp3'))
 for variant in variants: series[variant]=load('male_'+sound+'_'+variant)
 # Approximation of browser close-mic settings; FFmpeg compressor differs from WebAudio.
 raw=P/f'male_{sound}_wrapper.f32'
 result=subprocess.run(['ffmpeg','-v','error','-f','f32le','-ar',str(FS),'-ac','1','-i',str(raw),
  '-af','bass=g=9:f=120:t=q:w=0.707,equalizer=f=3500:t=q:w=0.8:g=3,acompressor=threshold=0.0398107:ratio=5:attack=2:release=120:knee=2.51189:makeup=2.2',
  '-f','f32le','-'],capture_output=True,check=True)
 series['mic_style']=np.frombuffer(result.stdout,dtype='<f4').copy()
 if sound=='t':
  for trial in ['half','quarter']:
   path=P/('hat_pulse_'+trial+'.f32')
   if path.exists(): series['pulse_'+trial]=load('hat_pulse_'+trial)
 all_data[sound]={};metrics[sound]={}
 for name,x in series.items():
  c,onset=crop(x);all_data[sound][name]=c
  metrics[sound][name]=features(c)|{'alignment_onset_ms':onset/48}
# Same active RMS target across every hit; lower globally to leave headroom on the most peaky hit.
def active_rms(x):
 env=signal.convolve(x.astype(float)**2,np.ones(240)/240,mode='same')
 mask=env>env.max()*.01
 return np.sqrt(np.mean(x[mask].astype(float)**2))
ratios=[np.max(np.abs(x))/active_rms(x) for data in all_data.values() for x in data.values()]
target=min(.07,.8/max(ratios))
listen_names={'reference':['Reference: heavy kick','Reference: snare','Reference: hat'],
 'current':['wrapper']*3,'native':['native_planned']*3,'mic_style':['mic_style']*3}
for label,names in listen_names.items():
 chunks=[]
 for sound,name in zip(['B','Pf','t'],names):
  x=all_data[sound][name].copy(); x=x*target/active_rms(x)
  x[:48]*=np.linspace(0,1,48); x[-240:]*=np.linspace(1,0,240) # export-only boundary fades
  chunks += [np.zeros(4800),x,np.zeros(24000)]
 wav(label+'_kit',np.concatenate(chunks))
# Quantitative evidence and reproducible data for the inline chart.
(P/'metrics.json').write_text(json.dumps({'features':metrics,'interventions':hypotheses,'listening_active_rms':target},indent=2)+'\n')
chart={}
for sound,data in all_data.items():
 chart[sound]={}
 for name,x in data.items():
  x=x/max(np.max(np.abs(x)),1e-15)
  f,t,z=signal.stft(x,fs=FS,nperseg=512,noverlap=416,boundary=None)
  db=20*np.log10(np.maximum(np.abs(z[f<=12000]),1e-4))
  chart[sound][name]={'wave':np.stack((x.reshape(600,32).min(axis=1),x.reshape(600,32).max(axis=1)),axis=1).round(5).tolist(),'spec':db.round(1).tolist(),'metrics':metrics[sound][name]}
html='''<!doctype html><meta charset="utf-8"><style>
body{font:14px var(--font-sans);color:var(--foreground)}label{margin-right:12px}select{color:var(--foreground);background:var(--secondary);border:1px solid var(--border);padding:6px} .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;margin-top:12px}.panel{padding:12px;background:var(--card);border:1px solid var(--border);border-radius:var(--radius)}canvas{width:100%;height:110px;display:block}h3{font-size:14px;margin:0 0 7px}p{line-height:1.5}small{color:var(--muted-foreground)}@media(max-width:480px){.grid{grid-template-columns:1fr}}
</style><p><b>Beatbox diagnostic: waveform + spectrogram</b><br><small>Choose a sound. All panels use peak-normalized amplitude, the same 400 ms window and −80 to 0 dB spectrogram scale. Alignment uses a 5 ms RMS envelope crossing 20% of its peak. This view compares shape, not trigger latency.</small></p>
<label>Sound <select id="sound"><option value="B">Kick</option><option value="Pf">Snare</option><option value="t" selected>Hat</option></select></label><label>Kick reference <select id="ref"><option>heavy kick</option><option>light kick</option></select></label><div class="grid" id="grid"></div><p><small>Spectrogram: 0–12 kHz, high frequencies at top; 512-sample Hann window, 96-sample hop. Native = future-scheduled CLI recipe on our pinned engine fork. Mic-style = approximate browser EQ/compression applied to current output. Reference previews are lossy MP3; exact high-frequency matching is not a valid target. No perceptual improvement is claimed.</small></p><script>
const data=DATA;
function draw(){const sound=document.querySelector('#sound').value;const d=data[sound];const ref='Reference: '+(sound==='B'?document.querySelector('#ref').value:sound==='Pf'?'snare':'hat');const names=[ref,'wrapper','native_planned','mic_style'];const titles=['Approved human reference','Current Phona','Native scheduled Chimera','Current + mic-style processing'];if(sound==='t' && d.pulse_half){names.push('pulse_half','pulse_quarter');titles.push('Half breath pulse','Quarter breath pulse')}grid.innerHTML='';names.forEach((name,i)=>{const a=d[name];const div=document.createElement('div');div.className='panel';div.innerHTML='<h3>'+titles[i]+'</h3><canvas width="600" height="140"></canvas><canvas width="600" height="140"></canvas><small>−20 → 380 ms · 90% energy span '+a.metrics.energy90_ms.toFixed(1)+' ms</small>';grid.append(div);let c=div.children[1],ctx=c.getContext('2d');ctx.strokeStyle=getComputedStyle(document.documentElement).getPropertyValue('--chart-1').trim()||'#5ba8f5';ctx.lineWidth=1;ctx.beginPath();a.wave.forEach((v,j)=>{ctx.moveTo(j,70-v[0]*65);ctx.lineTo(j,70-v[1]*65)});ctx.stroke();c=div.children[2];ctx=c.getContext('2d');const rows=a.spec.length,cols=a.spec[0].length;for(let r=0;r<rows;r++)for(let k=0;k<cols;k++){let v=Math.max(0,Math.min(1,(a.spec[r][k]+80)/80));ctx.fillStyle=`rgb(${Math.round(255*v)},${Math.round(210*v*v)},${Math.round(100*(1-v)*v)})`;ctx.fillRect(k*600/cols,140-(r+1)*140/rows,Math.ceil(600/cols),Math.ceil(140/rows));}})}
document.querySelectorAll('select').forEach(s=>s.onchange=draw);draw();</script>'''
(P/'comparison.html').write_text(html.replace('DATA',json.dumps(chart,separators=(',',':'))))
print('Saved metrics, four 3-second audition kits, and interactive waveform/spectrogram comparison.')
for sound,data in metrics.items():
 print(sound, {k:round(v['energy90_ms'],1) for k,v in data.items() if k in ['wrapper','native_planned','mic_style'] or k.startswith('Reference')})
