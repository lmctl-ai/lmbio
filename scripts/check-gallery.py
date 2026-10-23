#!/usr/bin/env python3
"""Check gallery sources, image signatures, dimensions and byte budgets."""
import pathlib,re,struct,sys
r=pathlib.Path(__file__).resolve().parent.parent
s=(r/'README.md').read_text()
prefix="https://raw.githubusercontent.com/lmctl-ai/"+r.name+"/main/examples/"
urls=re.findall(r'lmctl autorun (https://raw\.githubusercontent\.com/[^\s`|<>]+\.lms)',s)
assert len(urls)>=14, "Every gallery card and example listing needs an autorun command"
assert not re.search(r'\]\(examples/[^)]+\.lms\)',s), "Use autorun commands instead of relative source links"
for url in urls:
 assert url.startswith(prefix),url
 assert (r/'examples'/url.removeprefix(prefix)).is_file(),url
for path in re.findall(r'\]\((docs/images/[^)]+)\)',s):
 assert (r/path).is_file(), f"Missing image {path}"
for p in (r/'examples').glob('*.lms'):
 text=p.read_text()
 assert 'cli="lmplot" install="github:lmctl-ai/lmplot@latest"' in text,p
 assert 'examples/render-' not in text and 'examples/visuals' not in text,p
 assert 'images/'+p.stem+'.png' in text,p
 assert (r/'docs/images'/(p.stem+'.png')).is_file(),p
 assert prefix+p.name in urls,p
 if '--generated' in sys.argv:
  output=r/'images'/(p.stem+'.png')
  assert output.is_file() and output.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n',output
images=list((r/'docs/images').glob('*'))
assert len(images)>=8
for p in images:
 b=p.read_bytes()
 if p.suffix=='.png':
  assert b[:8]==b'\x89PNG\r\n\x1a\n', p
  w,h=struct.unpack('>II',b[16:24]);assert 850<=w<=1000 and h>=400,p
  assert len(b)<=300000,(p,len(b))
 elif p.suffix=='.gif':assert b[:6] in (b'GIF87a',b'GIF89a') and len(b)<=3000000,p
 else:raise AssertionError(p)
print(f"gallery: {len(images)} valid images, sources and size limits checked")
