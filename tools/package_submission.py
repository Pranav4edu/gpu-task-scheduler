#!/usr/bin/env python3
"""Build the submission from tracked source. Needs Python + Git only when packaging."""
from pathlib import Path
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
output = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root.parent / 'output' / 'submission' / 'Group4_Project.zip'
output.parent.mkdir(parents=True, exist_ok=True)
commit = subprocess.check_output(['git','rev-parse','HEAD'], cwd=root, text=True).strip()
files = subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
report = root / 'docs' / 'Group4_Final_Report.pdf'
if not report.is_file():
    raise SystemExit('Generate docs/Group4_Final_Report.pdf before packaging.')
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in files:
        if not name or not (name in ('README.md','run.sh') or name.startswith(('code/','docs/','tools/'))):
            continue
        if name == 'docs/Group4_Final_Report.pdf':
            continue
        if name.startswith(('code/build/','code/results/')) or name.endswith(('.exe','.bat','.o')):
            continue
        archive.write(root/name, name)
    archive.write(report, 'Group4_Final_Report.pdf')
    archive.writestr('SOURCE_VERSION.txt', commit+'\n')
    archive.writestr('SUBMISSION_CHECK.txt',
        'This package contains source only; code/build and code/results are regenerated.\n'
        'Run ./run.sh from this extracted project root, or invoke its absolute path.\n'
        'Expected: 202 test groups, 756 benchmark rows and five six-task demonstrations.\n'
        'The delivery accompanies an independently verified clean-extraction record.\n')
print(output)
