"""Fetch four user-approved public Bandcamp previews; no account or full-pack download."""
from pathlib import Path
import html,json,re,urllib.request,hashlib
root=Path(__file__).resolve().parent
url='https://vmdmusic.bandcamp.com/album/beatbox-sample-pack'
page=urllib.request.urlopen(url,timeout=20).read().decode()
data=json.loads(html.unescape(re.search(r'data-tralbum="([^"]+)"',page).group(1)))
names={'Heavy Kick':'heavy_kick','Light Kick':'light_kick','Punchy Snare':'snare','Closed Hi-Hat':'hat'}
manifest=[]
for track in data['trackinfo']:
 if track['title'] not in names: continue
 audio=urllib.request.urlopen(track['file']['mp3-128'],timeout=20).read()
 assert len(audio)<1000000
 path=root/('ref_'+names[track['title']]+'.mp3');path.write_bytes(audio)
 manifest.append(dict(title=track['title'],source=url,track_page='https://vmdmusic.bandcamp.com'+track['title_link'],
                      file=path.name,bytes=len(audio),sha256=hashlib.sha256(audio).hexdigest(),
                      format='public 128 kbps MP3 preview; not lossless measurement ground truth',
                      usage='private reference audition/analysis only; not shipped or committed'))
assert len(manifest)==4
(root/'references.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Fetched four approved previews:',sum(m['bytes'] for m in manifest),'bytes total')
