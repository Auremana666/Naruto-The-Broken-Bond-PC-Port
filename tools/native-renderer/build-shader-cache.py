"""The shader cache generated in the build from the local game files (inside the build
directory, git-ignored with it).

Usage: python -I build-shader-cache.py <xeshader.sdb> <XenosRecomp.exe> <shader_common.h> <dxc.exe> <out> [<store>]

Runs the chain of the shader cache on the SDB of the game: the containers
(extract-sdb-containers.py), the translation of each one by the XenosRecomp fork of
the repository and DXC (build-sdb-shader-cache.py), the lib_6_3 libraries of the
spec constants (build-sdb-shader-libraries.py), the variants those libraries give with
the values the draw asks for, linked here (build-sdb-shader-variants.py), the index,
format 4, identification only by hash (build-native-shader-index.py) and the package that
app/src/gpu/shaders/shader_cache_data.cpp embeds (build-embedded-shader-cache.py).
Writes <out>/shader_cache.bin and <out>/shader_cache.json; the intermediate steps go
to <out>/work, replaced on each run. Any shader that does not translate or compile
fails the build. With <store> (a folder kept across builds, outside the build directory),
the translation, DXIL, library and variants of each shader are reused when their inputs
did not change (shader_store.py); the package is the same bytes either way.
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
sdb, xenosrecomp, common, dxc, out = (Path(a).resolve() for a in sys.argv[1:6])
store = ['--store', Path(sys.argv[6]).resolve()] if len(sys.argv) > 6 and sys.argv[6] else []
for path in (sdb, xenosrecomp, common, dxc):
    if not path.is_file():
        sys.exit(f'build-shader-cache: missing {path}')
work = out / 'work'
if work.exists():
    shutil.rmtree(work)
work.mkdir(parents=True)
jobs = str(max(1, os.cpu_count() or 4))
# The spec constant values the draw asks for (draw.cpp): SPEC_CONSTANT_ALPHA_TEST, and
# SPEC_CONSTANT_ALPHA_TO_COVERAGE in its place for transparency antialiasing.
SPEC_VALUES = '2,8'


def run(script, *args):
    subprocess.run([sys.executable, '-I', str(here / script), *map(str, args)], check=True,
                   stdout=subprocess.DEVNULL)


def failures(report):
    # Mask 10 is the alpha test with the alpha to coverage variant (2 | 8); spec2 and
    # spec8 are those variants linked.
    counts = json.loads(report.read_text(encoding='utf-8'))['counts']
    return {k: v for k, v in counts.items() if not (k.endswith(':ok') or k.endswith(':ok:mask2') or
                                                    k.endswith(':ok:mask10') or
                                                    k.endswith(':no-spec-constants:mask0') or
                                                    k.endswith(':ok:spec2') or k.endswith(':ok:spec8'))}


containers = work / 'containers'
run('extract-sdb-containers.py', sdb, containers)
run('build-sdb-shader-cache.py', containers / 'containers', work / 'per-shader', '--jobs', jobs,
    '--xenosrecomp', xenosrecomp, '--common', common, '--dxc', dxc, *store)
run('build-sdb-shader-libraries.py', work / 'per-shader', work / 'libraries', '--jobs', jobs, '--dxc', dxc, *store)
run('build-sdb-shader-variants.py', work / 'libraries', work / 'variants', '--jobs', jobs, '--dxc', dxc,
    '--values', SPEC_VALUES, *store)
bad = {**failures(work / 'per-shader' / 'report.json'), **failures(work / 'libraries' / 'report.json'),
       **failures(work / 'variants' / 'report.json')}
if bad:
    sys.exit(f'build-shader-cache: shaders that did not build: {bad}')
run('build-native-shader-index.py', containers / 'containers', work / 'per-shader', work / 'index', work / 'libraries',
    work / 'variants')
run('build-embedded-shader-cache.py', work / 'index' / 'index.tsv', out)
identity = json.loads((out / 'shader_cache.json').read_text(encoding='utf-8'))
reused = {step: {kind: n for kind, n in json.loads((work / step / 'report.json').read_text(encoding='utf-8'))['store'].items()
                 if kind != 'root'} for step in ('per-shader', 'libraries', 'variants')}
print(f"shader cache: {identity['entries']} shaders, {identity['variant_files']} variants, "
      f"payload {identity['payload_sha256'][:16]}, store {json.dumps(reused)}")
