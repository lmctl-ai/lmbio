#!/usr/bin/env python3
"""Check gallery sources, image signatures, dimensions and byte budgets."""
import pathlib,re,struct
r=pathlib.Path(__file__).resolve().parent.parent
s=(r/'README.md').read_text()
for path in re.findall(r'\]\((examples/[^)]+\.lms)\)',s):
 assert (r/path).is_file(), f"Missing example {path}"
for path in re.findall(r'\]\((docs/images/[^)]+)\)',s):
 assert (r/path).is_file(), f"Missing image {path}"
for p in (r/'examples').glob('*.lms'):
 assert (r/'examples/visuals'/p.name).is_file(), p
 assert (r/'docs/images'/(p.stem+'.png')).is_file(), p
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
