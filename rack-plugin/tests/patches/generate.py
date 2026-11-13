#!/usr/bin/env python3
"""Rebuild two quiet Rack fixtures from the captured live module schema."""
import argparse
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCHEMA = json.loads((ROOT / 'schema.json').read_text())

def patch(held=False, beat=False):
    modules = []
    def module(model, pos, changes=None, data=None):
        schema = SCHEMA[model]
        m = dict(id=len(modules)+1, plugin='Core' if model=='Notes' else 'Phona' if model=='Phona' else 'Fundamental',
                 model=model, version='2.6.6' if model=='Notes' else '2.1.0' if model=='Phona' else '2.6.4',
                 pos=pos, params=[dict(id=p['id'], value=(changes or {}).get(p['id'],p['default'])) for p in schema['params']])
        if 'data' in schema: m['data']=copy.deepcopy(schema['data'])
        if data is not None: m['data']=data
        modules.append(m)
        return m['id']
    pitches=[0,2/12,4/12,5/12,7/12,9/12,11/12,1]
    seq=module('SEQ3',[0,0],{0:2 if beat else 0, **{4+i:v for i,v in enumerate(pitches)},
               **{12+i:v for i,v in enumerate([0,10,10,0,10,10,10,0])}},
               dict(running=False,gates=[1]*8,clockPassthrough=False))
    voice=module('Phona',[24,0],{2:0.547,14:0,15:0,13:0},dict(lyrics='B t Pf t B . Pf t' if beat else 'doh ray mee fah soh lah tee doh', beatbox=beat))
    lfo=module('LFO',[46,0],{0:0,2:-3})
    osc=module('VCO',[58,0],{2:-12})
    vca=module('VCA',[69,0],{0:0,1:0})
    mode='HELD: CV 2 -> GATE. Steps 2-3 and 5-7 hold one syllable across pitch changes.' if held else 'TRIGGERS: TRIG -> GATE. Eight short gates per cycle; first trigger at D3.'
    module('Notes',[0,1],data=dict(text='PHONA / QUIET ACCEPTANCE\nNo Audio module. VCA levels = 0.\nSEQ-3 is STOPPED. Press RESET, then RUN.\n'+mode+'\nCV 1: C D E F G A B C (C3 base). First RUN advances to step 2.\nRESET out -> Phona RESET.\nLFO sine -> VOWEL and NASAL; amounts start at 0.\nBODY: switch M/F on Phona.\nVCO SAW is parked: connect to EXT IN for talkbox.\nPhona OUT -> VCA channel 1.\nRead docs/live-instrument-test.md before adding Audio.\nSave changes to a COPY.'))
    if beat:
        modules[-1]['data']['text']='PHONA / BEATBOX PROTOTYPE\nNo Audio module. VCA levels = 0. SEQ-3 STOPPED.\nPress RESET, then RUN. Four triggers/second.\nPattern: B t Pf t B . Pf t\nB kick / Pf snare / t hat / . rest\nEach gate advances one token. RESET restarts the pattern.\nBODY changes the mouth; other knobs are inactive.\nMouth preparation delays the main hit; fast hits can cancel each other.\nRead docs/beatbox-prototype.md. Save edits to a COPY.'
    cables=[]
    def cable(out,op,inp,ip,color):
        cables.append(dict(id=len(cables)+1,outputModuleId=out,outputId=op,inputModuleId=inp,inputId=ip,color=color))
    if not beat: cable(seq,1,voice,0,'#e8b04c')
    cable(seq,2 if held else 0,voice,12,'#d65252')
    cable(seq,15,voice,16,'#7e68b0')
    if not beat:
        cable(lfo,0,voice,4,'#4a9ac9'); cable(lfo,0,voice,5,'#4a9ac9')
    cable(voice,0,vca,2,'#55ad72')
    return dict(version='2.6.6',modules=modules,cables=cables)

def validate(p):
    mods={m['id']:m for m in p['modules']}
    assert len(mods)==len(p['modules'])
    assert all(m['model'] in SCHEMA for m in mods.values())
    assert not any('Audio' in m['model'] for m in mods.values())
    for m in mods.values():
        specs=SCHEMA[m['model']]['params']
        assert len({v['id'] for v in m['params']})==len(specs)
        for v in m['params']:
            s=specs[v['id']]; assert s['min'] <= v['value'] <= s['max']
    inputs=set()
    for c in p['cables']:
        for side,ports in [('input','inputs'),('output','outputs')]:
            m=mods[c[side+'ModuleId']]
            assert 0 <= c[side+'Id'] < len(SCHEMA[m['model']][ports])
        key=(c['inputModuleId'],c['inputId']); assert key not in inputs; inputs.add(key)
    assert mods[1]['data']['running'] is False
    assert all(v['value']==0 for v in mods[5]['params'])

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--check',action='store_true'); args=parser.parse_args()
    for held,beat,name in [(False,False,'singing-triggers.vcv'),(True,False,'singing-held.vcv'),(False,True,'beatbox-triggers.vcv')]:
        p=patch(held,beat); validate(p); text=json.dumps(p,indent=2)+'\n'; file=ROOT/name
        if args.check: assert file.read_text()==text, f'{name}: regenerate fixture'
        else: file.write_text(text)
        print(f'PASS {name}: schema, routing, stopped transport, zero output gain')
