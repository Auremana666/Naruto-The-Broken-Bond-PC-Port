"""Packs the shader cache to embed it in the build.

The cache (index.tsv, the DXIL of each shader and the spec constant variants linked in
the build) becomes one compressed file,
shader_cache.bin, that app/src/gpu/shaders/shader_cache_data.cpp embeds
with #embed. Nothing is recompiled: the bytes are those of the cache. When the index
points at SPIR-V (a build with the Vulkan backend), a second file,
shader_cache_spirv.bin, holds the modules and the manifest; the renderer unpacks the
one of its API only.

Container (little endian): "NRSC", u32 version 1, u32 algorithm 2 (MSZIP of the
Windows Compression API, buffer mode), u32 0, u64 payload bytes, u64 compressed bytes,
32-byte SHA-256 of the payload, then the compressed payload.
Payload: "NRSP", u32 version, u32 index bytes, u32 files, the index text (the
lines of the binary directories say "embedded") padded to 4, one record per file (u32
kind; u32 name bytes; u32 offset into the blob area; u32 size; the name padded to 4),
then the blob area.
- DXIL, version 2: kind 0 the DXIL of a shader, 2 a variant named <name>+spec<value>.
  Version 1 had kind 1, the lib_6_3 libraries the renderer linked at run time.
- SPIR-V, version 5: kind 3 the module of a shader encoded by smol-v (lossless,
  smolv-encode), 4 the manifest (manifest.json): the spirv contract of
  build-shader-manifest.py with its contract_sha256, and the SHA-256 of each decoded
  module by name, 5 the module of a pass (build-pass-spirv.py) encoded by smol-v, by
  the name of the pass. The index has a contract line (contract_sha256), each shader
  line ends with the SHA-256 of its decoded module, and a line per pass gives its
  name, stage, layout and the SHA-256 of its decoded module. After the contract line,
  the contract's requirements and layouts, which the Vulkan backend checks against
  the device: "require" <capability|extension> <name>; "binding" <layout> <set>
  <binding> <type> <detail or ->, for the draw layout ("draw") and each pass layout;
  "push" <layout> <stage> <begin> <end>. Version 4 had no requirement and layout
  lines, version 3 no passes.

Usage: python -I build-embedded-shader-cache.py <index.tsv> <output dir> [<manifest.json> <passes smolv dir>]
Writes shader_cache.bin, shader_cache_spirv.bin with SPIR-V (the manifest and the
encoded passes are required then) and shader_cache.json (identity: inputs, counts,
sizes and SHA-256) into the output dir.
"""
import ctypes
import hashlib
import json
import struct
import sys
from pathlib import Path

MSZIP = 2
KIND_DXIL, KIND_VARIANT, KIND_SPIRV, KIND_MANIFEST, KIND_PASS = 0, 2, 3, 4, 5


def compress(data):
    cabinet = ctypes.WinDLL('cabinet.dll')
    handle = ctypes.c_void_p()
    if not cabinet.CreateCompressor(MSZIP, None, ctypes.byref(handle)):
        raise OSError('CreateCompressor')
    try:
        size = ctypes.c_size_t()
        cabinet.Compress(handle, data, len(data), None, 0, ctypes.byref(size))
        out = ctypes.create_string_buffer(size.value)
        if not cabinet.Compress(handle, data, len(data), out, size.value, ctypes.byref(size)):
            raise OSError('Compress')
        return out.raw[:size.value]
    finally:
        cabinet.CloseCompressor(handle)


def pad4(data):
    return data + b'\0' * (-len(data) % 4)


def package(version, lines, entries, binary):
    """Writes the container of one payload; returns its identity."""
    files, blobs, offset = [], [], 0
    for kind, name, data in entries:
        files.append((kind, name.encode('ascii'), offset, len(data)))
        blobs.append(pad4(data))
        offset += len(blobs[-1])
    index_bytes = ('\n'.join(lines) + '\n').encode('utf-8')
    payload = b'NRSP' + struct.pack('<III', version, len(index_bytes), len(files)) + pad4(index_bytes)
    for kind, name, at, size in files:
        payload += struct.pack('<IIII', kind, len(name), at, size) + pad4(name)
    payload += b''.join(blobs)
    packed = compress(payload)
    digest = hashlib.sha256(payload).digest()
    container = b'NRSC' + struct.pack('<IIIQQ', 1, MSZIP, 0, len(payload), len(packed)) + digest + packed
    binary.write_bytes(container)
    return dict(payload_bytes=len(payload), payload_sha256=digest.hex(), compressed_bytes=len(packed),
                container=dict(path=str(binary.resolve()), sha256=hashlib.sha256(container).hexdigest()))


def contract_lines(spirv):
    """The requirement and layout lines of the SPIR-V contract (the manifest's spirv)."""
    lines = [f'require\tcapability\t{name}' for name in spirv['required']['capabilities']]
    lines += [f'require\textension\t{name}' for name in spirv['required']['extensions']]
    lines += [f"binding\tdraw\t{b['set']}\t{b['binding']}\t{b['type']}\t-" for b in spirv['layout']['bindings']]
    lines.append(f"push\tdraw\tvertex+fragment\t0\t{spirv['layout']['push_constant_bytes']}")
    for name, layout in sorted(spirv['passes']['layouts'].items()):
        lines += [f"binding\t{name}\t{layout['set']}\t{b['binding']}\t{b['type']}\t{b.get('detail', '-')}"
                  for b in layout['bindings']]
        lines += [f'push\t{name}\t{stage}\t{begin}\t{end}'
                  for stage, (begin, end) in sorted(layout['push_constants'].items())]
    return lines


def main():
    index, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    manifest = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    passes_dir = Path(sys.argv[4]) if len(sys.argv) > 4 else None
    assert bool(manifest) == bool(passes_dir), 'the SPIR-V package needs the manifest and the passes'
    text = index.read_text(encoding='utf-8')
    lines, dirs, names = [], {}, []
    for line in text.splitlines():
        fields = line.split('\t')
        if len(fields) == 2 and fields[0] in ('dxil', 'variants', 'spirv'):
            dirs[fields[0]] = Path(fields[1])
            lines.append(f'{fields[0]}\tembedded')
            continue
        if fields[0] in ('vs', 'ps'):
            names.append(fields[1])
        lines.append(line)
    assert {'dxil', 'variants'} <= set(dirs) and len(names) == len(set(names))
    assert ('spirv' in dirs) == bool(manifest), 'the SPIR-V package needs the manifest'
    entries = [(KIND_DXIL, name, dirs['dxil'] / f'{name}.dxil') for name in names]
    for name in names:  # The variants of each shader, by value.
        found = {int(p.stem.rsplit('+spec', 1)[1]): p for p in dirs['variants'].glob(f'{name}+spec*.dxil')}
        entries += [(KIND_VARIANT, f'{name}+spec{value}', found[value]) for value in sorted(found)]
    for _, _, path in entries:
        assert path.exists(), f'missing DXIL {path}'
    out_dir.mkdir(parents=True, exist_ok=True)
    binary = out_dir / 'shader_cache.bin'
    dxil = package(2, [l for l in lines if not l.startswith('spirv\t')],
                   [(kind, name, path.read_bytes()) for kind, name, path in entries], binary)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    identity = dict(
        schema='native-embedded-shader-cache-v1', index=dict(path=str(index.resolve()), sha256=sha(index)),
        dxil_dir=str(dirs['dxil']), variants_dir=str(dirs['variants']), entries=len(names),
        dxil_files=sum(1 for e in entries if e[0] == KIND_DXIL),
        variant_files=sum(1 for e in entries if e[0] == KIND_VARIANT), **dxil,
        algorithm='MSZIP (Windows Compression API, buffer mode)',
        tool=dict(path=str(Path(__file__).resolve()), sha256=sha(Path(__file__))))
    if manifest:
        full = json.loads(manifest.read_text(encoding='utf-8'))
        embedded = dict(schema=full['schema'], contract_sha256=full['contract_sha256'], spirv=full['spirv'],
                        shaders={name: full['shaders'][name]['spirv'] for name in names}, passes=full['passes'])
        modules = [(KIND_SPIRV, name, (dirs['spirv'] / f'{name}.smolv').read_bytes()) for name in names]
        modules.append((KIND_MANIFEST, 'manifest.json',
                        json.dumps(embedded, sort_keys=True, separators=(',', ':')).encode('utf-8')))
        modules += [(KIND_PASS, name, (passes_dir / f'{name}.smolv').read_bytes()) for name in sorted(full['passes'])]
        # The index of the SPIR-V package: the contract and the passes after the spirv line,
        # and each shader with the SHA-256 of its decoded module, which the renderer checks.
        described = full['spirv']['passes']['passes']
        spirv_lines = []
        for line in lines:
            fields = line.split('\t')
            if fields[0] in ('dxil', 'variants'):
                continue
            spirv_lines.append(f"{line}\t{embedded['shaders'][fields[1]]}" if fields[0] in ('vs', 'ps') else line)
            if fields[0] == 'spirv':
                spirv_lines.append(f"contract\t{full['contract_sha256']}")
                spirv_lines += contract_lines(full['spirv'])
                spirv_lines += [f"pass\t{name}\t{described[name]['stage']}\t{described[name]['layout']}\t{module}"
                                for name, module in sorted(full['passes'].items())]
        identity['spirv'] = dict(
            dir=str(dirs['spirv']), modules=len(names), passes=len(full['passes']), passes_dir=str(passes_dir),
            manifest=dict(path=str(manifest.resolve()), sha256=sha(manifest), contract_sha256=full['contract_sha256']),
            **package(5, spirv_lines, modules, out_dir / 'shader_cache_spirv.bin'))
    (out_dir / 'shader_cache.json').write_text(json.dumps(identity, indent=1) + '\n', encoding='utf-8')
    print(json.dumps({k: identity[k] for k in ('entries', 'dxil_files', 'variant_files', 'payload_bytes',
                                               'compressed_bytes', 'payload_sha256')}))


if __name__ == '__main__':
    main()
