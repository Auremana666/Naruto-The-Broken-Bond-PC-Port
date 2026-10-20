"""The SPIR-V of each translated shader, for the Vulkan backend of the native renderer.

Usage: python -I build-sdb-shader-spirv.py <cache dir> <new output dir> --dxc <dxc.exe> --spirv-val <spirv-val.exe>
       --masks <libraries report.json> [--jobs N] [--store <dir>]

Reads the HLSL of a per-shader cache (build-sdb-shader-cache.py), the text the DXIL
comes from, and compiles its __spirv__ branch for the environment of the pinned Plume
revision (Vulkan 1.2, SPIR-V 1.5): the arguments of the fork's DxcCompiler for SPIR-V
(-fvk-use-dx-layout, -fvk-invert-y in a VS), the target environment and the stage
I/O locations in declaration order, which the fork keeps the same in both stages
(the interpolators TEXCOORD0..15, COLOR0 and COLOR1 at 0..17). The g_SpecConstants()
stub of the DXIL path is taken off, as build-sdb-shader-libraries.py does: SPIR-V
reads constant_id(0), given when the pipeline is created. Each module goes through
spirv-val in the same environment, and its interface (spirv_module.py) is recorded.

The report checks the corpus against the layout the Vulkan backend builds:
- the descriptor bindings (2D, 3D and cube textures in sets 0 to 2, samplers in set 3,
  each an array at binding 0) and the 24-byte push constant block (the addresses of the
  VS, PS and shared constants);
- the stage interface: every VS writes Position, the six clip distances and the 18
  interpolators; a PS reads only those (and FragCoord, FrontFacing);
- the vertex inputs: the [[vk::location]] of each element, as the fork's table gives it
  in the HLSL, is the location of the SPIR-V input, distinct inside each VS, raw uint4;
- the specialization: no VS declares a specialization constant, and a PS declares only
  constant_id 0, the one its spec mask reads. One VkSpecializationInfo with the PS value
  for every stage (the pinned Plume gives the same one to all) then specializes each
  stage as the DXIL variants do.
A shader that does not compile or validate, or breaks a check, is reported; the caller
fails the build. With --store, the SPIR-V of a shader is reused when its HLSL, the
arguments and DXC did not change, and the verdict of spirv-val when the module, its
arguments and spirv-val did not.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))  # shader_store (the scripts run with python -I).
import shader_store
import spirv_module

STUB = '\nuint g_SpecConstants() { return 0; }\n'
TARGET_ENVIRONMENT = 'vulkan1.2'
INTERPOLATORS = 18
VERTEX_INPUT = re.compile(r'^\t\[\[vk::location\((\d+)\)\]\] in (\w+) i\w+ : ([A-Z]+)(\d+),$', re.M)
BINDINGS = {(0, 0): 'image2D<float>[]', (1, 0): 'image3D<float>[]', (2, 0): 'imageCube<float>[]', (3, 0): 'sampler[]'}
PUSH_CONSTANT_BYTES = 24

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('cache', type=Path)
parser.add_argument('out', type=Path)
parser.add_argument('--dxc', type=Path, required=True)
parser.add_argument('--spirv-val', type=Path, required=True)
parser.add_argument('--masks', type=Path, required=True,
                    help='the report of build-sdb-shader-libraries.py, with the spec mask of each shader')
parser.add_argument('--jobs', type=int, default=12)
parser.add_argument('--store', type=Path, help='the content-addressed store (shader_store.py); none if absent')
args = parser.parse_args()
dxc, spirv_val = args.dxc.resolve(), args.spirv_val.resolve()
cache, out = args.cache.resolve(), args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
for name in ('hlsl', 'spirv', 'log'):
    (out / name).mkdir()
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
store = shader_store.Store(args.store)
dxc_id = shader_store.dxc_identity(dxc)
VALIDATOR = [str(spirv_val), '--target-env', TARGET_ENVIRONMENT]
validator_id = shader_store.files_identity([spirv_val])


def arguments(pixel: bool):
    return ['-T', 'ps_6_0' if pixel else 'vs_6_0', '-E', 'main', '-HV', '2021', '-spirv',
            f'-fspv-target-env={TARGET_ENVIRONMENT}', '-fvk-use-dx-layout', '-fvk-stage-io-order=decl',
            *([] if pixel else ['-fvk-invert-y']), '-Qstrip_debug']


def check(entry: dict, interface: dict, text: str):
    """The layout and interface checks of one module; the names of those it breaks."""
    broken = []
    pixel = entry['stage'] == 'ps'
    for b in interface['bindings']:
        if BINDINGS.get((b['set'], b['binding'])) != b['type']:
            broken.append('binding')
    if interface['push_constant_bytes'] != PUSH_CONSTANT_BYTES:
        broken.append('push-constant')
    located = lambda items: {e['location']: e['type'] for e in items if 'location' in e}
    builtins = lambda items: {e['builtin']: e['type'] for e in items if 'builtin' in e}
    if pixel:
        if any(t != 'float4' or not 0 <= loc < INTERPOLATORS for loc, t in located(interface['inputs']).items()):
            broken.append('pixel-inputs')
        if not set(builtins(interface['inputs'])) <= {'FragCoord', 'FrontFacing'}:
            broken.append('pixel-builtins')
        if set(interface['spec_constants']) - {'0'} or (entry['mask'] and '0' not in interface['spec_constants']):
            broken.append('pixel-spec-constants')
    else:
        if located(interface['outputs']) != {loc: 'float4' for loc in range(INTERPOLATORS)}:
            broken.append('vertex-interpolators')
        if builtins(interface['outputs']) != {'Position': 'float4', 'ClipDistance': 'float[6]'}:
            broken.append('vertex-builtins')
        declared = [(int(loc), kind, f'{usage}{index}') for loc, kind, usage, index in VERTEX_INPUT.findall(text)]
        entry['vertex_inputs'] = {semantic: loc for loc, _, semantic in declared}
        inputs = located(interface['inputs'])
        if (len({loc for loc, _, _ in declared}) != len(declared) or set(inputs) != {loc for loc, _, _ in declared}
                or any(t != 'uint4' for t in inputs.values()) or any(kind != 'uint4' for _, kind, _ in declared)):
            broken.append('vertex-inputs')
        if interface['spec_constants']:
            broken.append('vertex-spec-constants')
    return broken


def one(hlsl: Path, mask: int):
    stem = hlsl.stem
    pixel = stem.startswith('ps-')
    text = hlsl.read_text(encoding='utf-8')
    entry = dict(name=stem, stage='ps' if pixel else 'vs', mask=mask, hlsl_sha256=sha(hlsl))
    source = out / 'hlsl' / hlsl.name
    source.write_text(text[:-len(STUB)] + '\n' if text.endswith(STUB) else text, encoding='utf-8')
    spirv = out / 'spirv' / (stem + '.spv')
    flags = arguments(pixel)
    compiled = shader_store.key('dxc', source.read_bytes(), *flags, dxc_id)
    blob = store.get('spirv', compiled)
    if blob is not None:
        spirv.write_bytes(blob)
        entry['dxc'] = 'store'
    else:
        run = subprocess.run([str(dxc), *flags, '-Fo', str(spirv), str(source)], capture_output=True, timeout=300)
        if run.returncode or not spirv.exists():
            (out / 'log' / (stem + '.dxc.txt')).write_bytes(run.stdout + run.stderr)
            first = next((l for l in (run.stdout + run.stderr).decode('utf-8', 'replace').splitlines()
                          if 'error:' in l), '')
            entry['first_error'] = first.split('error:', 1)[-1].strip()
            entry['result'] = 'compile-failed'
            return entry
        store.put('spirv', compiled, spirv.read_bytes())
    entry['spirv_sha256'] = sha(spirv)
    entry['spirv_bytes'] = spirv.stat().st_size
    validated = shader_store.key('spirv-val', spirv.read_bytes(), *VALIDATOR[1:], validator_id)
    if store.get('validated', validated) is not None:
        entry['spirv_val'] = 'store'
    else:
        validation = subprocess.run([*VALIDATOR, str(spirv)], capture_output=True, timeout=300)
        if validation.returncode:
            (out / 'log' / (stem + '.spirv-val.txt')).write_bytes(validation.stdout + validation.stderr)
            entry['first_error'] = (validation.stdout + validation.stderr).decode('utf-8', 'replace').strip()[:300]
            entry['result'] = 'validation-failed'
            return entry
        store.put('validated', validated, b'ok')
    interface = spirv_module.parse(spirv.read_bytes())
    entry['interface'] = interface
    broken = check(entry, interface, text)
    if broken:
        entry['broken'] = broken
        entry['result'] = 'check-failed'
        return entry
    entry['result'] = 'ok'
    return entry


masks = {e['name']: e['mask'] for e in json.loads(args.masks.read_text(encoding='utf-8'))['results']}
started = datetime.datetime.now(datetime.timezone.utc)
with ThreadPoolExecutor(args.jobs) as pool:
    shaders = sorted((cache / 'hlsl').glob('*.hlsl'))
    results = list(pool.map(lambda h: one(h, masks[h.stem]), shaders))
finished = datetime.datetime.now(datetime.timezone.utc)
counts, errors, broken = {}, {}, {}
for r in results:
    counts[f"{r['stage']}:{r['result']}"] = counts.get(f"{r['stage']}:{r['result']}", 0) + 1
    if r.get('first_error'):
        errors[r['first_error']] = errors.get(r['first_error'], 0) + 1
    for b in r.get('broken', ()):
        broken[f"{r['stage']}:{b}"] = broken.get(f"{r['stage']}:{b}", 0) + 1
ok = [r for r in results if r['result'] == 'ok']
union = lambda field: sorted({c for r in ok for c in r['interface'][field]})
# The vertex input locations of the corpus by semantic (one each: the fork's table), and the
# semantics the table puts at one location (never inside one VS, checked above).
locations, semantics = {}, {}
for r in ok:
    for semantic, loc in r.get('vertex_inputs', {}).items():
        locations.setdefault(semantic, set()).add(loc)
        semantics.setdefault(loc, set()).add(semantic)
summary = dict(
    target_environment=TARGET_ENVIRONMENT, spirv_versions=sorted({r['interface']['version'] for r in ok}),
    capabilities=union('capabilities'), extensions=union('extensions'),
    # The contract every module was checked against, and what the corpus uses of it.
    layout=dict(bindings=[dict(set=s, binding=b, type=t) for (s, b), t in sorted(BINDINGS.items())],
                push_constant_bytes=PUSH_CONSTANT_BYTES),
    stage_interface=dict(interpolators=INTERPOLATORS, interpolator_type='float4', clip_distances=6,
                         vertex_input_type='uint4', io_order='decl', vertex_y='inverted (-fvk-invert-y)'),
    bindings=sorted({(b['set'], b['binding'], b['type']) for r in ok for b in r['interface']['bindings']}),
    push_constant_bytes=sorted({r['interface']['push_constant_bytes'] for r in ok}),
    spec_constants={stage: sorted({k for r in ok if r['stage'] == stage for k in r['interface']['spec_constants']})
                    for stage in ('vs', 'ps')},
    spec_masks={stage: sorted({r['mask'] for r in results if r['stage'] == stage}) for stage in ('vs', 'ps')},
    vertex_input_locations={s: sorted(v) for s, v in sorted(locations.items())},
    locations_shared_in_the_corpus={loc: sorted(s) for loc, s in sorted(semantics.items()) if len(s) > 1},
    specialization_route='shared' if not broken and all(r['result'] == 'ok' for r in results) else None)
report = dict(started_at_utc=started.isoformat(), finished_at_utc=finished.isoformat(), cache=str(cache),
              arguments=dict(vs=arguments(False), ps=arguments(True), spirv_val=VALIDATOR[1:]),
              tools=dict(dxc=sha(dxc), spirv_val=sha(spirv_val), script=sha(__file__),
                         spirv_module=sha(Path(spirv_module.__file__)), dxc_files=dxc_id),
              store=store.summary(), containers=len(results), counts=dict(sorted(counts.items())),
              broken=dict(sorted(broken.items())), top_errors=sorted(errors.items(), key=lambda kv: -kv[1])[:40],
              summary=summary, results=results)
(out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
print(json.dumps(dict(counts=report['counts'], broken=report['broken'], store=report['store'],
                      seconds=(finished - started).total_seconds(), top_errors=report['top_errors'][:12],
                      summary={k: v for k, v in summary.items() if k != 'vertex_input_locations'}), indent=1))
