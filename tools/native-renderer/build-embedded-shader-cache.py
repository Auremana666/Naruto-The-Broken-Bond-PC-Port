"""Packs the shader cache to embed it in the build.

The cache (index.tsv, the DXIL of each shader and the spec constant variants linked in
the build) becomes one compressed file,
shader_cache.bin, that app/src/gpu/shaders/shader_cache_data.cpp embeds
with #embed. Nothing is recompiled: the bytes are those of the cache.

Container (little endian): "NRSC", u32 version 1, u32 algorithm 2 (MSZIP of the
Windows Compression API, buffer mode), u32 0, u64 payload bytes, u64 compressed bytes,
32-byte SHA-256 of the payload, then the compressed payload.
Payload: "NRSP", u32 version 2, u32 index bytes, u32 files, the index text (the
dxil and variants lines say "embedded") padded to 4, one record per file (u32 kind: 0
DXIL, 2 variant named <name>+spec<value>; u32 name bytes; u32 offset into the blob area;
u32 size; the name padded to 4), then the blob area. Version 1 had kind 1, the lib_6_3
libraries the renderer linked at run time.

Usage: python -I build-embedded-shader-cache.py <index.tsv> <output dir>
Writes shader_cache.bin and shader_cache.json
(identity: inputs, counts, sizes and SHA-256) into the output dir.
"""
import ctypes
import hashlib
import json
import struct
import sys
from pathlib import Path

MSZIP = 2


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


def main():
    index, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    text = index.read_text(encoding='utf-8')
    lines, dirs, names = [], {}, []
    for line in text.splitlines():
        fields = line.split('\t')
        if len(fields) == 2 and fields[0] in ('dxil', 'variants'):
            dirs[fields[0]] = Path(fields[1])
            lines.append(f'{fields[0]}\tembedded')
            continue
        if fields[0] in ('vs', 'ps'):
            names.append(fields[1])
        lines.append(line)
    assert set(dirs) == {'dxil', 'variants'} and len(names) == len(set(names))
    entries = [(0, name, dirs['dxil'] / f'{name}.dxil') for name in names]
    for name in names:  # The variants of each shader, by value.
        found = {int(p.stem.rsplit('+spec', 1)[1]): p for p in dirs['variants'].glob(f'{name}+spec*.dxil')}
        entries += [(2, f'{name}+spec{value}', found[value]) for value in sorted(found)]
    files, blobs, offset = [], [], 0
    for kind, name, path in entries:
        assert path.exists(), f'missing DXIL {path}'
        data = path.read_bytes()
        files.append((kind, name.encode('ascii'), offset, len(data)))
        blobs.append(pad4(data))
        offset += len(blobs[-1])
    index_bytes = ('\n'.join(lines) + '\n').encode('utf-8')
    payload = b'NRSP' + struct.pack('<III', 2, len(index_bytes), len(files)) + pad4(index_bytes)
    for kind, name, at, size in files:
        payload += struct.pack('<IIII', kind, len(name), at, size) + pad4(name)
    payload += b''.join(blobs)
    packed = compress(payload)
    digest = hashlib.sha256(payload).digest()
    container = b'NRSC' + struct.pack('<IIIQQ', 1, MSZIP, 0, len(payload), len(packed)) + digest + packed
    out_dir.mkdir(parents=True, exist_ok=True)
    binary = out_dir / 'shader_cache.bin'
    binary.write_bytes(container)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    identity = dict(
        schema='native-embedded-shader-cache-v1', index=dict(path=str(index.resolve()), sha256=sha(index)),
        dxil_dir=str(dirs['dxil']), variants_dir=str(dirs['variants']), entries=len(names),
        dxil_files=sum(1 for f in files if f[0] == 0), variant_files=sum(1 for f in files if f[0] == 2),
        payload_bytes=len(payload), payload_sha256=digest.hex(), compressed_bytes=len(packed),
        algorithm='MSZIP (Windows Compression API, buffer mode)', container=dict(path=str(binary.resolve()),
                                                                               sha256=sha(binary)),
        tool=dict(path=str(Path(__file__).resolve()), sha256=sha(Path(__file__))))
    (out_dir / 'shader_cache.json').write_text(json.dumps(identity, indent=1) + '\n', encoding='utf-8')
    print(json.dumps({k: identity[k] for k in ('entries', 'dxil_files', 'variant_files', 'payload_bytes',
                                               'compressed_bytes', 'payload_sha256')}))


if __name__ == '__main__':
    main()
