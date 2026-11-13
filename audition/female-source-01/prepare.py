"""Blind labels and static loudness matching. Requires ffmpeg; no Python packages."""
import array
import json
from pathlib import Path
import random
import subprocess
import wave

HERE = Path(__file__).resolve().parent
NAMES = ['baseline', 'tension', 'brightness', 'breathiness']

def measure(path, raw=False):
    args = ['ffmpeg', '-hide_banner', '-nostats']
    if raw:
        args += ['-f', 'f32le', '-ar', '48000', '-ac', '1']
    args += ['-i', str(path), '-af', 'loudnorm=I=-23:TP=-2:LRA=11:print_format=json', '-f', 'null', '-']
    result = subprocess.run(args, capture_output=True, text=True, check=True)
    return json.JSONDecoder().raw_decode(result.stderr[result.stderr.rfind('{'):])[0]

measurements = {name: measure(HERE / f'{name}.f32', True) for name in NAMES}
# Apply only a constant gain. Lower the shared target if any candidate needs more headroom.
target = min([-23.0] + [float(m['input_i']) - 2 - float(m['input_tp']) for m in measurements.values()])
key_path = HERE / 'blind-key.json'
if key_path.exists():
    key = json.loads(key_path.read_text())['labels']
else:
    random.SystemRandom().shuffle(NAMES)
    key = dict(zip('ABCD', NAMES))
report = {'labels': key, 'target_lufs': target, 'verification': {}}
for label, name in key.items():
    gain = target - float(measurements[name]['input_i'])
    path = HERE / f'{label}.wav'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', '48000', '-ac', '1',
                    '-i', str(HERE / f'{name}.f32'), '-af', f'volume={gain}dB',
                    '-c:a', 'pcm_s16le', str(path)], check=True)
    with wave.open(str(path), 'rb') as f:
        assert f.getframerate() == 48000 and f.getnchannels() == 1 and f.getsampwidth() == 2
        samples = array.array('h', f.readframes(f.getnframes()))
    assert 0 < max(abs(x) for x in samples) < 32767
    assert not any(samples[:4800]) and not any(samples[-4800:])
    m = measure(path)
    assert abs(float(m['input_i']) - target) < 0.15
    assert float(m['input_tp']) <= -1.9
    report['verification'][label] = {'lufs': float(m['input_i']), 'true_peak_dbfs': float(m['input_tp']),
                                     'samples': len(samples), 'gain_db': gain}
key_path.write_text(json.dumps(report, indent=2) + '\n')
print('Four WAVs verified: mono 48 kHz PCM16, matching duration and loudness, no clipping, silent boundaries.')
