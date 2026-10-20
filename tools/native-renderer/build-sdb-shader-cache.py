"""Translates and compiles each container extracted from the SDB on its own.

Usage: python -I build-sdb-shader-cache.py <containers dir> <new output dir> [--jobs N]
       --xenosrecomp <XenosRecomp.exe> --common <shader_common.h> --dxc <dxc.exe> [--store <dir>]

--xenosrecomp and --common select the fork of the repository (tools/xenosrecomp,
NARUTO_RECOMP).

The directory mode of XenosRecomp aborts the batch at the first DXC error. Here each
container goes through the single-file mode (HLSL) and through dxc.exe with the
arguments of DxcCompiler (vs_6_0/ps_6_0, -HV 2021, -all-resources-bound), plus the
g_SpecConstants() stub. Any shader that does not translate or compile is reported.
With --store, the translation and the DXIL of each shader are reused from the
content-addressed store (shader_store.py) when their inputs did not change.
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

STUB = '\nuint g_SpecConstants() { return 0; }\n'

# Vertex texture fetch. A Xenos vertex shader samples fetch constants 16 to
# 19 (its sampler table counts them from 0), and XenosRecomp names them by the
# instruction (s16_..._DescriptorIndex) without declaring them, so those VS do
# not compile ("undeclared identifier 's16_Texture1DDescriptorIndex'").
# Their descriptor indices go to c18 (texture) and c19 (sampler) of the shared
# constants, one component per register (the engine writes them at bytes 288 and
# 304 + 4 * (register - 16)); c0 to c15 stay for registers 0 to 15. The SPIR-V
# branch reads the same bytes by the address of the shared constants, as
# XenosRecomp defines the indices of registers 0 to 15 there. A 1D texture
# is bound as a 2D texture one texel high and sampled at level 0, as a vertex
# shader has no derivatives. XenosRecomp has no vertex textures; this is a
# change to its output.
VERTEX_SAMPLER = re.compile(r'\bs(1[6-9])_(Texture1D|Texture2D|Texture3D|TextureCube|Sampler)DescriptorIndex\b')
TFETCH1D = ('float4 tfetch1D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float texCoord)\n'
            '{\n'
            '    return g_Texture2DDescriptorHeap[resourceDescriptorIndex].SampleLevel('
            'g_SamplerDescriptorHeap[samplerDescriptorIndex], float2(texCoord, 0.5), 0);\n'
            '}\n\n')


def patch_vertex_textures(text: str):
    """Declares the vertex sampler indices s16..s19 and tfetch1D; returns (text, registers) or raises."""
    found = sorted(set(VERTEX_SAMPLER.findall(text)))
    if not found:
        return text, []
    kinds = {}
    for register, kind in found:
        if kind != 'Sampler':
            kinds.setdefault(register, set()).add(kind)
    if any(len(k) > 1 for k in kinds.values()):
        raise ValueError('vertex-texture-dimensions-conflict')
    declarations = ''.join(
        f'\tuint s{register}_{kind}DescriptorIndex : packoffset(c{19 if kind == "Sampler" else 18}.{"xyzw"[int(register) - 16]});\n'
        for register, kind in found)
    anchor = '\tDEFINE_SHARED_CONSTANTS();'
    if text.count(anchor) != 1:
        raise ValueError('shared-constants-anchor')
    text = text.replace(anchor, declarations + anchor, 1)
    addresses = ''.join(
        f'#define s{register}_{kind}DescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + '
        f'{(304 if kind == "Sampler" else 288) + 4 * (int(register) - 16)})\n' for register, kind in found)
    anchor = '#else\n\ncbuffer VertexShaderConstants'
    if text.count(anchor) != 1:
        raise ValueError('spirv-constants-anchor')
    text = text.replace(anchor, addresses + '\n' + anchor, 1)
    if 'tfetch1D(' in text and 'float4 tfetch1D(' not in text:
        anchor = 'cbuffer VertexShaderConstants'
        if text.count(anchor) != 1:
            raise ValueError('vertex-constants-anchor')
        text = text.replace(anchor, TFETCH1D + anchor, 1)
    return text, sorted({int(r) for r, _ in found})

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('containers', type=Path)
parser.add_argument('out', type=Path)
parser.add_argument('--jobs', type=int, default=12)
parser.add_argument('--xenosrecomp', type=Path, required=True)
parser.add_argument('--common', type=Path, required=True)
parser.add_argument('--dxc', type=Path, required=True)
parser.add_argument('--store', type=Path, help='the content-addressed store (shader_store.py); none if absent')
args = parser.parse_args()
xrc, common, dxc = args.xenosrecomp.resolve(), args.common.resolve(), args.dxc.resolve()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
for name in ('hlsl', 'dxil', 'log'):
    (out / name).mkdir()
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
# The identities of the store keys: the fork's sources (not the rebuilt
# XenosRecomp.exe), this script (the patches above) and the DXC files.
store = shader_store.Store(args.store)
fork, script, dxc_id = shader_store.fork_identity(common), sha(__file__), shader_store.dxc_identity(dxc)


def translate(container: Path, hlsl: Path, pixel: bool, entry: dict) -> bool:
    """The fork's HLSL of a container with the patches of this script; False on failure (entry says why)."""
    stem = container.stem
    run = subprocess.run([str(xrc), str(container), str(hlsl), str(common)], capture_output=True, timeout=120)
    entry['xenosrecomp_rc'] = run.returncode
    if run.returncode or not hlsl.exists() or not hlsl.stat().st_size:
        (out / 'log' / (stem + '.xrc.txt')).write_bytes(run.stdout + run.stderr)
        entry['result'] = 'translate-failed'
        return False
    text = hlsl.read_text(encoding='utf-8', errors='replace')
    if not pixel:
        try:
            text, vertex_samplers = patch_vertex_textures(text)
        except ValueError as error:
            entry['result'] = f'patch-failed-{error}'
            return False
        if vertex_samplers:
            entry['vertex_samplers'] = vertex_samplers
    # shader_common.h declares g_SpecConstants() as external on the DXIL path.
    if 'uint g_SpecConstants();' in text and 'uint g_SpecConstants() {' not in text:
        text += STUB
        entry['spec_constants_stub'] = True
    hlsl.write_text(text, encoding='utf-8')
    return True


def one(container: Path):
    stem = container.stem
    pixel = stem.startswith('ps-')
    hlsl = out / 'hlsl' / (stem + '.hlsl')
    data = container.read_bytes()
    entry = dict(file=container.name, stage='ps' if pixel else 'vs', container_sha256=hashlib.sha256(data).hexdigest())
    # The store keeps the HLSL file as written (CRLF on Windows) after a JSON
    # line with the fields the translation adds to the entry.
    translation = shader_store.key('translate', data, fork, script)
    cached = store.get('translate', translation, compressed=True)
    if cached is not None:
        fields, text = cached.split(b'\n', 1)
        entry.update(json.loads(fields))
        hlsl.write_bytes(text)
        entry['translation'] = 'store'
    else:
        if not translate(container, hlsl, pixel, entry):
            return entry
        fields = {k: entry[k] for k in ('xenosrecomp_rc', 'vertex_samplers', 'spec_constants_stub') if k in entry}
        store.put('translate', translation, json.dumps(fields).encode() + b'\n' + hlsl.read_bytes(), compressed=True)
    dxil = out / 'dxil' / (stem + '.dxil')
    arguments = ['-T', 'ps_6_0' if pixel else 'vs_6_0', '-HV', '2021', '-all-resources-bound',
                 '-Wno-ignored-attributes', '-Qstrip_reflect', '-Qstrip_debug']
    compiled = shader_store.key('dxc', hlsl.read_bytes(), *arguments, dxc_id)
    blob = store.get('dxil', compiled)
    if blob is not None:
        dxil.write_bytes(blob)
        entry['dxc_rc'] = 0
        entry['dxc'] = 'store'
    else:
        compile_ = subprocess.run([str(dxc), *arguments, '-Fo', str(dxil), str(hlsl)], capture_output=True, timeout=300)
        entry['dxc_rc'] = compile_.returncode
        if compile_.returncode or not dxil.exists():
            (out / 'log' / (stem + '.dxc.txt')).write_bytes(compile_.stdout + compile_.stderr)
            first = next((l for l in (compile_.stdout + compile_.stderr).decode('utf-8', 'replace').splitlines()
                          if 'error:' in l), '')
            entry['first_error'] = first.split('error:', 1)[-1].strip()
            entry['result'] = 'compile-failed'
            return entry
        store.put('dxil', compiled, dxil.read_bytes())
    entry['dxil_sha256'] = sha(dxil)
    entry['result'] = 'ok'
    return entry


started = datetime.datetime.now(datetime.timezone.utc)
containers = sorted(args.containers.glob('*.bin'))
with ThreadPoolExecutor(args.jobs) as pool:
    results = list(pool.map(one, containers))
finished = datetime.datetime.now(datetime.timezone.utc)
counts = {}
for r in results:
    counts[(r['stage'], r['result'])] = counts.get((r['stage'], r['result']), 0) + 1
errors = {}
for r in results:
    if r.get('first_error'):
        errors[r['first_error']] = errors.get(r['first_error'], 0) + 1
report = dict(started_at_utc=started.isoformat(), finished_at_utc=finished.isoformat(),
              tools=dict(xenosrecomp=sha(xrc), shader_common=sha(common), dxc=sha(dxc), script=sha(__file__),
                         fork_sources=fork, dxc_files=dxc_id),
              store=store.summary(), containers=len(results),
              counts={f'{s}:{r}': n for (s, r), n in sorted(counts.items())},
              top_errors=sorted(errors.items(), key=lambda kv: -kv[1])[:40], results=results)
(out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
print(json.dumps(dict(containers=len(results), counts=report['counts'], store=report['store'],
                      seconds=(finished - started).total_seconds(), top_errors=report['top_errors'][:12]), indent=1))
