"""The shader cache generated in the build from the local game files (inside the build
directory, git-ignored with it).

Usage: python -I build-shader-cache.py <xeshader.sdb> <XenosRecomp.exe> <shader_common.h> <dxc.exe> <out> [<store>]
       [--spirv-val <spirv-val.exe> --smolv-encode <smolv-encode.exe> --passes <app/src/gpu/backend>]

Runs the chain of the shader cache on the SDB of the game: the containers
(extract-sdb-containers.py), the translation of each one by the XenosRecomp fork of
the repository and DXC (build-sdb-shader-cache.py), the lib_6_3 libraries of the
spec constants (build-sdb-shader-libraries.py), the variants those libraries give with
the values the draw asks for, linked here (build-sdb-shader-variants.py), the index,
format 4, identification only by hash (build-native-shader-index.py) and the package that
app/src/gpu/shaders/shader_cache_data.cpp embeds (build-embedded-shader-cache.py).
With --spirv-val, --smolv-encode and --passes (a build with the Vulkan backend), the
SPIR-V of each shader too, validated and checked against the Vulkan layout
(build-sdb-shader-spirv.py), and the SPIR-V of the renderer's own passes, FXAA and
the SDK's output effects from the backend folder (build-pass-spirv.py), encoded by
smol-v (smolv-encode.cpp), described by the manifest (build-shader-manifest.py) and
packaged on their own. Writes
<out>/shader_cache.bin and <out>/shader_cache.json, and with SPIR-V
<out>/shader_cache_spirv.bin and <out>/shader_manifest.json; the intermediate steps
go to <out>/work, replaced on each run. Any shader that does not translate, compile or
validate, or breaks a check, fails the build. With <store> (a folder kept across
builds, outside the build directory), the translation, DXIL, library, variants and
SPIR-V of each shader are reused when their inputs did not change (shader_store.py);
the packages are the same bytes either way.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
for name in ('sdb', 'xenosrecomp', 'common', 'dxc', 'out'):
    parser.add_argument(name, type=Path)
parser.add_argument('store', nargs='?', default='')
parser.add_argument('--spirv-val', type=Path)
parser.add_argument('--smolv-encode', type=Path)
parser.add_argument('--passes', type=Path, help='the backend folder of the passes (build-pass-spirv.py)')
args = parser.parse_args()
if len({bool(args.spirv_val), bool(args.smolv_encode), bool(args.passes)}) != 1:
    sys.exit('build-shader-cache: SPIR-V needs --spirv-val, --smolv-encode and --passes')
sdb, xenosrecomp, common, dxc, out = (p.resolve() for p in (args.sdb, args.xenosrecomp, args.common, args.dxc, args.out))
store = ['--store', Path(args.store).resolve()] if args.store else []
for path in (sdb, xenosrecomp, common, dxc, *([args.spirv_val, args.smolv_encode] if args.spirv_val else [])):
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
steps = ['per-shader', 'libraries', 'variants']
if args.spirv_val:
    run('build-sdb-shader-spirv.py', work / 'per-shader', work / 'spirv', '--jobs', jobs, '--dxc', dxc,
        '--spirv-val', args.spirv_val.resolve(), '--masks', work / 'libraries' / 'report.json', *store)
    run('build-pass-spirv.py', args.passes.resolve(), work / 'passes', '--dxc', dxc,
        '--spirv-val', args.spirv_val.resolve(), *store)
    steps += ['spirv', 'passes']
bad = {k: v for step in steps for k, v in failures(work / step / 'report.json').items()}
if bad:
    sys.exit(f'build-shader-cache: shaders that did not build: {bad}')
manifest = out / 'shader_manifest.json'
encoded = []
if args.spirv_val:
    # The modules of the shaders and of the passes, each step into its smolv folder.
    encoded = [work / step / 'smolv' for step in ('spirv', 'passes')]
    listing = work / 'smolv.tsv'
    with listing.open('w', encoding='utf-8') as lines:
        for folder in encoded:
            folder.mkdir()
            for spv in sorted((folder.parent / 'spirv').glob('*.spv')):
                lines.write(f'{spv}\t{folder / (spv.stem + ".smolv")}\n')
    subprocess.run([str(args.smolv_encode.resolve()), str(listing)], check=True, stdout=subprocess.DEVNULL)
    run('build-shader-manifest.py', work, manifest)
else:  # A build without the Vulkan backend embeds no SPIR-V: no package of an earlier build stays.
    for stale in (out / 'shader_cache_spirv.bin', manifest):
        stale.unlink(missing_ok=True)
run('build-native-shader-index.py', containers / 'containers', work / 'per-shader', work / 'index', work / 'libraries',
    work / 'variants', *encoded[:1])
run('build-embedded-shader-cache.py', work / 'index' / 'index.tsv', out, *([manifest, encoded[1]] if encoded else []))
identity = json.loads((out / 'shader_cache.json').read_text(encoding='utf-8'))
reused = {step: {kind: n for kind, n in json.loads((work / step / 'report.json').read_text(encoding='utf-8'))['store'].items()
                 if kind != 'root'} for step in steps}
spirv = identity.get('spirv')
print(f"shader cache: {identity['entries']} shaders, {identity['variant_files']} variants, "
      f"payload {identity['payload_sha256'][:16]}" +
      (f", SPIR-V payload {spirv['payload_sha256'][:16]} ({spirv['compressed_bytes']} bytes packed, "
       f"{spirv['passes']} passes), contract {spirv['manifest']['contract_sha256'][:16]}" if spirv else '') +
      f", store {json.dumps(reused)}")
