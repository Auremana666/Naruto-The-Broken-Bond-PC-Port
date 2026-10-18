"""The lib_6_3 libraries of the shaders with spec constants.

Usage: python -I build-sdb-shader-libraries.py <cache dir> <new output dir> [--jobs N] --dxc <dxc.exe>
       [--store <dir>]

Reads the HLSL of a per-shader cache (build-sdb-shader-cache.py) and, for each shader
whose body uses spec constants, compiles the same HLSL without the g_SpecConstants()
stub with -T lib_6_3, as XenosRecomp does when the mask is not zero. At run time the
library is linked by IDxcLinker with another one that exports g_SpecConstants() with
the value of the draw. Mask by the body of main: SPEC_CONSTANT_ALPHA_TEST (2) and
SPEC_CONSTANT_ALPHA_TO_COVERAGE (8) in the PS, tfetchR11G11B10 (1) in the VS. With --store, a
library is reused from the content-addressed store (shader_store.py) when its HLSL, the
arguments and DXC did not change.
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

STUB = '\nuint g_SpecConstants() { return 0; }\n'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('cache', type=Path)
parser.add_argument('out', type=Path)
parser.add_argument('--jobs', type=int, default=12)
parser.add_argument('--dxc', type=Path, required=True)
parser.add_argument('--store', type=Path, help='the content-addressed store (shader_store.py); none if absent')
args = parser.parse_args()
dxc = args.dxc.resolve()
cache, out = args.cache.resolve(), args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
for name in ('hlsl', 'lib', 'log'):
    (out / name).mkdir()
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
store = shader_store.Store(args.store)
dxc_id = shader_store.dxc_identity(dxc)
ARGUMENTS = ['-T', 'lib_6_3', '-HV', '2021', '-all-resources-bound', '-Wno-ignored-attributes', '-Qstrip_reflect',
             '-Qstrip_debug']


def mask_of(text: str) -> int:
    body = text[text.rfind('void main('):]
    return ((2 if 'SPEC_CONSTANT_ALPHA_TEST' in body else 0) | (1 if 'tfetchR11G11B10(' in body else 0) |
            (8 if 'SPEC_CONSTANT_ALPHA_TO_COVERAGE' in body else 0))


def one(hlsl: Path):
    stem = hlsl.stem
    text = hlsl.read_text(encoding='utf-8')
    entry = dict(name=stem, hlsl_sha256=sha(hlsl), mask=mask_of(text))
    if not entry['mask']:
        entry['result'] = 'no-spec-constants'
        return entry
    if not text.endswith(STUB):
        entry['result'] = 'stub-missing'
        return entry
    source = out / 'hlsl' / hlsl.name
    source.write_text(text[:-len(STUB)] + '\n', encoding='utf-8')
    lib = out / 'lib' / (stem + '.dxil')
    compiled = shader_store.key('dxc', source.read_bytes(), *ARGUMENTS, dxc_id)
    blob = store.get('lib', compiled)
    if blob is not None:
        lib.write_bytes(blob)
        entry['dxc_rc'] = 0
        entry['dxc'] = 'store'
    else:
        run = subprocess.run([str(dxc), *ARGUMENTS, '-Fo', str(lib), str(source)], capture_output=True, timeout=300)
        entry['dxc_rc'] = run.returncode
        if run.returncode or not lib.exists():
            (out / 'log' / (stem + '.dxc.txt')).write_bytes(run.stdout + run.stderr)
            entry['result'] = 'compile-failed'
            return entry
        store.put('lib', compiled, lib.read_bytes())
    entry['lib_sha256'] = sha(lib)
    entry['result'] = 'ok'
    return entry


started = datetime.datetime.now(datetime.timezone.utc)
with ThreadPoolExecutor(args.jobs) as pool:
    results = list(pool.map(one, sorted((cache / 'hlsl').glob('*.hlsl'))))
finished = datetime.datetime.now(datetime.timezone.utc)
counts = {}
for r in results:
    key = f"{r['name'][:2]}:{r['result']}:mask{r['mask']}"
    counts[key] = counts.get(key, 0) + 1
report = dict(started_at_utc=started.isoformat(), finished_at_utc=finished.isoformat(), cache=str(cache),
              tools=dict(dxc=sha(dxc), script=sha(__file__), dxc_files=dxc_id), store=store.summary(),
              counts=dict(sorted(counts.items())), results=results)
(out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
print(json.dumps(dict(counts=report['counts'], store=report['store'], seconds=(finished - started).total_seconds()),
                 indent=1))
