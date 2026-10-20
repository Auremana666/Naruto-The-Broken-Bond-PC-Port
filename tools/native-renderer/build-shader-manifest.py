"""The manifest of the shader cache: every shader's identity in each binary format, and
the contract the Vulkan backend builds on.

Usage: python -I build-shader-manifest.py <work dir> <output manifest.json>

Reads the reports of the chain (build-shader-cache.py) in <work dir>: the DXIL of each
shader (per-shader), its spec constant variants (variants), its SPIR-V with the
checks of build-sdb-shader-spirv.py (spirv) and the SPIR-V of the passes with the
checks of build-pass-spirv.py (passes). Writes native-shader-manifest-v2:
- shaders: by name, the stage and the SHA-256 of the container, the DXIL, each variant
  by value and the SPIR-V module;
- passes: by name, the SHA-256 of the SPIR-V module of each pass;
- dxil: the tools and the spec constant values linked;
- spirv: the environment and arguments; the capabilities and extensions the corpus
  and the passes require; the layout every shader was checked against (descriptor
  sets and push constants) and the bindings it uses; the stage interface; the vertex
  input location of each semantic, the fork's table as the compiler wrote it; the
  specialization route (shared: one VkSpecializationInfo with the PS value for every
  stage); the passes: their pipeline layouts and, by pass, the layout, stage, origin
  (the HLSL with its arguments, or the SDK's module), the identity of the sources,
  the bindings and push constants it declares, and the clip space of a VS.
contract_sha256 identifies the spirv section, shaders_sha256 the shaders table and
passes_sha256 the passes table (canonical JSON). The Vulkan backend checks its device,
layouts and pipelines against the contract (V3 to V5 of the plan);
build-embedded-shader-cache.py embeds the contract and the SPIR-V hashes in the package.
"""
import hashlib
import json
import sys
from pathlib import Path

work, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
read = lambda step: json.loads((work / step / 'report.json').read_text(encoding='utf-8'))
canonical = lambda value: hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
per_shader, variants, spirv, passes = read('per-shader'), read('variants'), read('spirv'), read('passes')
if any(step['broken'] or any(r['result'] != 'ok' for r in step['results']) for step in (spirv, passes)):
    sys.exit('build-shader-manifest: the SPIR-V steps have failures')

shaders = {}
for r in per_shader['results']:
    name = Path(r['file']).stem
    shaders[name] = dict(stage=r['stage'], container=r['container_sha256'], dxil=r['dxil_sha256'])
for r in variants['results']:
    shaders[r['name']].setdefault('dxil_variants', {})[str(r['value'])] = r['dxil_sha256']
for r in spirv['results']:
    shaders[r['name']]['spirv'] = r['spirv_sha256']
summary = spirv['summary']
both = lambda field: sorted(set(summary[field]) | set(passes['summary'][field]))
INTERFACE = ('capabilities', 'bindings', 'push_constant_offset', 'push_constant_bytes')
contract = dict(
    target_environment=summary['target_environment'], spirv_versions=both('spirv_versions'),
    arguments=spirv['arguments'], validator=dict(arguments=spirv['arguments']['spirv_val'],
                                                 sha256=spirv['tools']['spirv_val']),
    required=dict(capabilities=both('capabilities'), extensions=both('extensions')),
    layout=summary['layout'],
    used_bindings=[dict(set=s, binding=b, type=t) for s, b, t in summary['bindings']],
    stage_interface=summary['stage_interface'], vertex_input_locations=summary['vertex_input_locations'],
    specialization=dict(route=summary['specialization_route'], constant_id=0,
                        spec_constants=summary['spec_constants'], spec_masks=summary['spec_masks']),
    passes=dict(arguments=passes['arguments']['stages'], layouts=passes['layouts'],
                passes={r['name']: dict(layout=r['layout'], stage=r['stage'], origin=r['origin'], sources=r['sources'],
                                        **({'entry': r['entry'], 'defines': r['defines']} if r['origin'] == 'hlsl'
                                           else {}),
                                        **({'vertex_y': r['vertex_y']} if 'vertex_y' in r else {}),
                                        version=r['interface']['version'],
                                        **{k: r['interface'][k] for k in INTERFACE})
                        for r in passes['results']}),
    encoding='smol-v, lossless (no flags), decoded and compared in the build')
pass_modules = {r['name']: r['spirv_sha256'] for r in passes['results']}
manifest = dict(schema='native-shader-manifest-v2', shaders_sha256=canonical(shaders),
                passes_sha256=canonical(pass_modules), contract_sha256=canonical(contract),
                dxil=dict(tools=per_shader['tools'], spec_values=variants['values']), spirv=contract,
                shaders=dict(sorted(shaders.items())), passes=dict(sorted(pass_modules.items())))
output.write_text(json.dumps(manifest, indent=1) + '\n', encoding='utf-8')
print(json.dumps(dict(shaders=len(shaders), passes=len(pass_modules), contract_sha256=manifest['contract_sha256'],
                      shaders_sha256=manifest['shaders_sha256'], passes_sha256=manifest['passes_sha256'])))
