"""Packs the staged release with forward-slash zip paths and a neutral source path."""
import json, sys, zipfile
from pathlib import Path

release = Path(sys.argv[1]); version = sys.argv[2]
src = release / 'src' / f'WoS-Symbiote-Rage-{version}-source'
inventory = src / 'attack_inventory.json'
data = json.loads(inventory.read_text())
data['source'] = 'MEGACITY/0x348760AC.Spiderman_rvb.als (game animation set, read locally; not shipped)'
inventory.write_text(json.dumps(data, indent=2), encoding='utf-8')

def pack(folder, zip_path, prefix=''):
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(folder.rglob('*')):
            if f.is_file():
                z.write(f, prefix + f.relative_to(folder).as_posix())
pack(release / 'main', release / f'WoS-Symbiote-Rage-{version}.zip')
pack(src, release / f'WoS-Symbiote-Rage-{version}-source.zip', f'WoS-Symbiote-Rage-{version}-source/')
