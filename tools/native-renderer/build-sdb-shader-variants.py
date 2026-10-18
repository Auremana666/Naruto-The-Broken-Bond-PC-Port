"""The spec constant variants of the shaders, linked in the build (as re:Blue's reblue_prelink).

Usage: python -I build-sdb-shader-variants.py <libraries dir> <new output dir> --dxc <dxc.exe>
       --values 2,8 [--jobs N] [--store <dir>]

For each lib_6_3 library of build-sdb-shader-libraries.py, and each value the draw can
ask for (--values; draw.cpp asks for SPEC_CONSTANT_ALPHA_TEST 2, and TransparencySpec
turns it into SPEC_CONSTANT_ALPHA_TO_COVERAGE 8), the value masked by the shader's mask
is linked once: a library exporting g_SpecConstants() with that value (the source the
renderer compiled at run time before) and the shader library, by `dxc -link`, into
<out>/dxil/<name>+spec<value>.dxil. With dxil.dll next to dxc.exe the result is signed
and byte for byte the one IDxcLinker gave at run time (same DXC). The renderer then
takes the variants from the embedded cache and needs no DXC. With --store, a variant
is reused from the content-addressed store (shader_store.py).
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))  # shader_store (the scripts run with python -I).
import shader_store

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('libraries', type=Path)
parser.add_argument('out', type=Path)
parser.add_argument('--dxc', type=Path, required=True)
parser.add_argument('--values', required=True, help='spec constant values the draw asks for, comma separated')
parser.add_argument('--jobs', type=int, default=12)
parser.add_argument('--store', type=Path, help='the content-addressed store (shader_store.py); none if absent')
args = parser.parse_args()
dxc = args.dxc.resolve()
libraries, out = args.libraries.resolve(), args.out.resolve()
values = sorted({int(v) for v in args.values.split(',')})
out.mkdir(parents=True, exist_ok=False)
for name in ('spec', 'dxil', 'log'):
    (out / name).mkdir()
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
store = shader_store.Store(args.store)
dxc_id = shader_store.dxc_identity(dxc)


def spec_library(value: int) -> Path:
    """The library exporting g_SpecConstants() with the value, as the renderer compiled it (-T lib_6_3 only)."""
    source = out / 'spec' / f'spec{value}.hlsl'
    source.write_bytes(f'export uint g_SpecConstants() {{ return {value}; }}'.encode())
    library = out / 'spec' / f'spec{value}.lib'
    compiled = shader_store.key('dxc', source.read_bytes(), '-T', 'lib_6_3', dxc_id)
    blob = store.get('spec', compiled)
    if blob is not None:
        library.write_bytes(blob)
    else:
        subprocess.run([str(dxc), '-T', 'lib_6_3', '-Fo', str(library), str(source)], check=True, capture_output=True,
                       timeout=300)
        store.put('spec', compiled, library.read_bytes())
    return library


report_in = json.loads((libraries / 'report.json').read_text(encoding='utf-8'))
masks = {e['name']: e['mask'] for e in report_in['results'] if e['result'] == 'ok'}
spec_libraries = {v: spec_library(v) for v in sorted({v & m for m in set(masks.values()) for v in values} - {0})}
jobs = [(name, value) for name, mask in sorted(masks.items()) for value in sorted({v & mask for v in values} - {0})]


def one(job):
    name, value = job
    library = libraries / 'lib' / f'{name}.dxil'
    variant = out / 'dxil' / f'{name}+spec{value}.dxil'
    entry = dict(name=name, value=value)
    profile = 'vs_6_0' if name.startswith('vs-') else 'ps_6_0'
    linked = shader_store.key('link', spec_libraries[value].read_bytes(), library.read_bytes(), profile, 'main', dxc_id)
    blob = store.get('variant', linked)
    if blob is not None:
        variant.write_bytes(blob)
        entry['dxc'] = 'store'
    else:
        run = subprocess.run([str(dxc), '-link', '-T', profile, '-E', 'main', f'{spec_libraries[value]};{library}',
                              '-Fo', str(variant)], capture_output=True, timeout=300)
        if run.returncode or not variant.exists():
            (out / 'log' / f'{name}+spec{value}.txt').write_bytes(run.stdout + run.stderr)
            entry['result'] = 'link-failed'
            return entry
        store.put('variant', linked, variant.read_bytes())
    entry['dxil_sha256'] = sha(variant)
    entry['result'] = 'ok'
    return entry


started = datetime.datetime.now(datetime.timezone.utc)
with ThreadPoolExecutor(args.jobs) as pool:
    results = list(pool.map(one, jobs))
finished = datetime.datetime.now(datetime.timezone.utc)
counts = {}
for r in results:
    key = f"{r['name'][:2]}:{r['result']}:spec{r['value']}"
    counts[key] = counts.get(key, 0) + 1
report = dict(started_at_utc=started.isoformat(), finished_at_utc=finished.isoformat(), libraries=str(libraries),
              values=values, tools=dict(dxc=sha(dxc), script=sha(__file__), dxc_files=dxc_id),
              spec_libraries={v: sha(p) for v, p in spec_libraries.items()}, store=store.summary(),
              counts=dict(sorted(counts.items())), results=results)
(out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
print(json.dumps(dict(counts=report['counts'], store=report['store'], seconds=(finished - started).total_seconds()),
                 indent=1))
