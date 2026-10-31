#!/usr/bin/env python3
"""Run the actual gallery gate under a different clone name; keep URL refusal live."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='gallery path control ') as directory:
    root = Path(directory) / 'arbitrary checkout name'
    (root / 'scripts').mkdir(parents=True)
    for name in ('README.md', 'scripts/check-gallery.py'):
        shutil.copy2(ROOT / name, root / name)
    for name in ('examples', 'docs/images'):
        shutil.copytree(ROOT / name, root / name)
    command = ['python3', str(root / 'scripts/check-gallery.py')]
    good = subprocess.run(command, capture_output=True)
    assert good.returncode == 0, good.stderr
    readme = root / 'README.md'
    readme.write_text(readme.read_text().replace('raw.githubusercontent.com/lmctl-ai/lmbio/main/',
                                                'raw.githubusercontent.com/wrong-owner/lmbio/main/'))
    bad = subprocess.run(command, capture_output=True)
    assert bad.returncode != 0 and b'AssertionError' in bad.stderr and b'wrong-owner' in bad.stderr, bad
print('PASS arbitrary checkout name; CAUGHT wrong owner at gallery prefix assertion')
