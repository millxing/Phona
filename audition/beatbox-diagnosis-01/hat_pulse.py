"""One-variable breath-pulse experiment. Run after the ignored Rust hat test."""
import analyze as a
import numpy as np
import json
baseline=a.load('hat_pulse_baseline')
current=a.load('male_t_wrapper')
assert np.array_equal(baseline[:len(current)],current), 'Custom scheduling changed the baseline'
results={};chunks=[]
for label,name in [('reference',None),('current','baseline'),('half','half'),('quarter','quarter')]:
 x=a.decode(a.P/'ref_hat.mp3') if name is None else a.load('hat_pulse_'+name)
 c,_=a.crop(x);results[label]=a.features(c)
 c=c*a.target/a.active_rms(c)
 c[:48]*=np.linspace(0,1,48);c[-240:]*=np.linspace(1,0,240)
 chunks.extend([np.zeros(4800),c,np.zeros(24000)])
a.wav('hat_pulse_comparison',np.concatenate(chunks))
(a.P/'hat-pulse-metrics.json').write_text(json.dumps(results,indent=2)+'\n')
print('Hat pulse energy90 ms:',{k:round(v['energy90_ms'],1) for k,v in results.items()})
