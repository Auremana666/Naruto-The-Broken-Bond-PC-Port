"""Extract validated Xenos shader containers from Rise/Broken Bond SDBs.

Usage: python -I extract-sdb-containers.py <sdb> <new output dir>

V1 retains the Rise bytewise scan. V4 uses 44-byte records and individual
zlib streams, measured against the supported Broken Bond SDB. Original data
and extracted/compiled shaders must remain outside version control.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

MAGIC = 0x33424453
RISE_VERSION = 0x00010001
BROKEN_BOND_VERSION = 0x00040001
MAX_CONTAINER_BYTES = 16 * 1024 * 1024


def container_info(blob):
    if len(blob) < 36:
        raise ValueError("Truncated shader container")
    flags, virtual, physical, _, _, _, shader, last1, last2 = struct.unpack_from('>9I', blob)
    if not (flags & 0xFFFFFF00 == 0x102A1100 and last1 == last2 == 0 and
            virtual >= 36 and physical > 0 and virtual + physical == len(blob) and
            0 < shader < virtual - 24):
        raise ValueError("Invalid shader container layout")
    code_offset, code_size = struct.unpack_from('>2I', blob, shader)
    if not code_size or code_offset + code_size > physical:
        raise ValueError("Shader code outside physical data")
    return dict(stage='vs' if flags & 1 else 'ps', virtual_bytes=virtual,
                physical_bytes=physical, code_bytes=code_size)


def read_containers(data):
    if len(data) < 24:
        raise ValueError("Truncated SDB header")
    magic, version, _, count, _, table = struct.unpack_from('>6I', data)
    if magic != MAGIC:
        raise ValueError("Unsupported SDB magic")
    if version == RISE_VERSION:
        cursor = 0
        while (start := data.find(b'\x10\x2a\x11', cursor)) >= 0:
            cursor = start + 1
            if start + 36 > len(data):
                continue
            virtual, physical = struct.unpack_from('>2I', data, start + 4)
            if virtual + physical > len(data) - start:
                continue
            blob = data[start:start + virtual + physical]
            try:
                info = container_info(blob)
            except ValueError:
                continue
            yield start, blob, info, None
        return
    if version != BROKEN_BOND_VERSION:
        raise ValueError(f"Unsupported SDB version 0x{version:08X}")
    # Observed v4 capacity; refuse variants until independently mapped.
    capacity, stride = 0x4000, 44
    payload_start = table + capacity * stride
    if not (0 < count <= capacity and table >= 24 and payload_start <= len(data)):
        raise ValueError("Invalid v4 shader table bounds")
    if any(data[table + count * stride:payload_start]):
        raise ValueError("Nonzero v4 unused table records")
    expected_offset = payload_start
    for index in range(count):
        record = table + index * stride
        offset, packed_size, decoded_size = struct.unpack_from('>3I', data, record + 32)
        if (offset != expected_offset or not packed_size or
                offset + packed_size > len(data) or
                not 36 <= decoded_size <= MAX_CONTAINER_BYTES):
            raise ValueError(f"Invalid v4 payload bounds in record {index}")
        decoder = zlib.decompressobj()
        try:
            blob = decoder.decompress(data[offset:offset + packed_size], decoded_size + 1)
        except zlib.error as error:
            raise ValueError(f"Invalid zlib stream in record {index}") from error
        if (not decoder.eof or decoder.unused_data or decoder.unconsumed_tail or
                len(blob) != decoded_size):
            raise ValueError(f"Invalid decoded size or stream boundary in record {index}")
        info = container_info(blob)
        yield offset, blob, info, dict(record=index, record_offset=hex(record),
                                     compressed_bytes=packed_size, decoded_bytes=decoded_size)
        expected_offset += packed_size
    if expected_offset != len(data):
        raise ValueError("Unexplained trailing bytes after v4 shader payloads")


def extract(source, out):
    if out.exists():
        raise ValueError("Output already exists; select a new output directory")
    data = source.read_bytes()
    # Validate the entire layout before materializing any containers.
    decoded = list(read_containers(data))
    if not decoded:
        raise ValueError("No valid shader containers found")
    out.mkdir(parents=True)
    (out / 'containers').mkdir()
    entries, seen = [], {}
    for start, blob, info, record in decoded:
        digest = hashlib.sha256(blob).hexdigest().upper()
        if digest in seen:
            seen[digest]['offsets'].append(hex(start))
            if record is not None:
                seen[digest]['records'].append(record)
            continue
        name = f'{info["stage"]}-{start:08X}.bin'
        (out / 'containers' / name).write_bytes(blob)
        entry = dict(**info, file=name, offsets=[hex(start)], bytes=len(blob), sha256=digest)
        if record is not None:
            entry['records'] = [record]
        seen[digest] = entry
        entries.append(entry)
    version = struct.unpack_from('>I', data, 4)[0]
    report = dict(source=str(source), source_sha256=hashlib.sha256(data).hexdigest().upper(),
                  sdb_version=f'0x{version:08X}',
                  layout_source='hedge-dev/XenosRecomp@990d03b28a27b50277ee5d8d942e1c5f873869d1: shader.h',
                  record_layout_source='local Broken Bond v4 analysis' if version == BROKEN_BOND_VERSION else None,
                  scan_alignment='v4-records-zlib' if version == BROKEN_BOND_VERSION else 'bytewise',
                  structurally_valid_headers=len(decoded), unique_containers=len(entries),
                  vertex=sum(e['stage'] == 'vs' for e in entries), pixel=sum(e['stage'] == 'ps' for e in entries),
                  extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), containers=entries)
    (out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    result = extract(Path(sys.argv[1]), Path(sys.argv[2]))
    print(json.dumps({k: result[k] for k in ['sdb_version', 'structurally_valid_headers',
                                           'unique_containers', 'vertex', 'pixel']}))
