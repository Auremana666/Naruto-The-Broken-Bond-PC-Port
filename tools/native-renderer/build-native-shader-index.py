"""Index of the shader cache for the own draw.

Usage: python -I build-native-shader-index.py <containers dir> <cache dir> <new output dir> <libraries dir>
       <variants dir>

Format 4: the first line is "format\t4", the second points at the DXIL directory.
For each container of the SDB compiled into <cache dir>/dxil, one line holds the
stage, the name, the 64-bit FNV-1a of the virtual part followed by the microcode
(bytes as in the guest), the virtual size and the use of textures in the HLSL body.
The runtime identifies a guest shader object by that hash alone. Containers without
DXIL stay out of the index.

A third line points at the spec constant variants linked in the build
(build-sdb-shader-variants.py, <name>+spec<value>.dxil), and each line gains the spec
constant mask of the shader (from the report of build-sdb-shader-libraries.py); the
runtime takes the variant of the value the draw asks for, masked.
"""
from pathlib import Path
import datetime
import hashlib
import json
import re
import struct
import sys

containers, cache, out, libraries, variants = (Path(a).resolve() for a in sys.argv[1:6])
masks = {e['name']: e['mask'] for e in json.loads((libraries / 'report.json').read_text(encoding='utf-8'))['results']}
out.mkdir(parents=True, exist_ok=False)


def fnv64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


lines = ['format\t4', f'dxil\t{cache / "dxil"}', f'variants\t{variants / "dxil"}']
counts = dict(indexed=0, without_dxil=0, textures=0, with_mask=0, without_variants=0)
for path in sorted(containers.glob('*.bin')):
    dxil = cache / 'dxil' / (path.stem + '.dxil')
    if not dxil.exists():
        counts['without_dxil'] += 1
        continue
    blob = path.read_bytes()
    virtual, physical = struct.unpack_from('>2I', blob, 4)
    shader = struct.unpack_from('>I', blob, 0x18)[0]
    code_offset, code_size = struct.unpack_from('>2I', blob, shader)
    code = blob[virtual + code_offset:virtual + code_offset + code_size]
    hlsl = (cache / 'hlsl' / (path.stem + '.hlsl')).read_text(encoding='utf-8', errors='replace')
    body = hlsl[hlsl.rfind('void main('):]
    # tfetchTexcoord and tfetchR11G11B10 are vertex fetches, not texture fetches.
    textures = int(bool(re.search(r'tfetch(1D|2D|3D|Cube)|getWeights2D|getPixelCoord', body)))  # 1D: vertex textures
    counts['textures'] += textures
    fields = [path.stem[:2], path.stem, f'{fnv64(blob[:virtual] + code):016x}', str(virtual), str(textures)]
    mask = masks[path.stem]
    fields.append(str(mask))
    counts['with_mask'] += bool(mask)
    counts['without_variants'] += bool(mask) and not any((variants / 'dxil').glob(path.stem + '+spec*.dxil'))
    lines.append('\t'.join(fields))
    counts['indexed'] += 1
(out / 'index.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
report = dict(at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), containers=str(containers),
              cache=str(cache), libraries=str(libraries), variants=str(variants), counts=counts,
              index_sha256=hashlib.sha256((out / 'index.tsv').read_bytes()).hexdigest(),
              tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report))
