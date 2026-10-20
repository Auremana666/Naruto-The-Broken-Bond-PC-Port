"""Correlate DXC missing sampler names with extracted constant-table ranges.

Reads local artifacts only. A register covered by metadata does not establish
correct HLSL, resource binding, sampling behavior or runtime execution.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct


def samplers(data):
    if len(data) < 36:
        raise ValueError('Truncated container')
    virtual = struct.unpack_from('>I', data, 4)[0]
    table = struct.unpack_from('>I', data, 16)[0] + 4
    if virtual > len(data) or table < 4 or table + 28 > virtual:
        raise ValueError('Constant table outside virtual data')
    count, info = struct.unpack_from('>2I', data, table + 12)
    if table + info + count * 20 > virtual:
        raise ValueError('Constant records outside virtual data')
    result = []
    for index in range(count):
        offset = table + info + index * 20
        name, kind, register, size, _, _, _ = struct.unpack_from('>I4H2I', data, offset)
        if kind != 3:
            continue
        start = table + name
        if not table <= start < virtual:
            raise ValueError('Constant name outside table')
        end = data.find(b'\0', start, virtual)
        if end < 0:
            raise ValueError('Unterminated constant name')
        result.append(dict(name=data[start:end].decode('ascii'), register=register,
                           count=size, record_offset=hex(offset)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('extraction', type=Path)
    parser.add_argument('compilation', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output exists; select a new report path')
    sha = lambda data: hashlib.sha256(data).hexdigest()
    compile_bytes = (args.compilation / 'report.json').read_bytes()
    rows, totals = [], Counter()
    for item in json.loads(compile_bytes)['results']:
        if item['result'] == 'ok':
            continue
        filename = item['file']
        if Path(filename).name != filename:
            raise ValueError('Expected a container filename')
        data = (args.extraction / 'containers' / filename).read_bytes()
        if sha(data) != item['container_sha256']:
            raise ValueError('Container identity differs from compilation input')
        ranges = samplers(data)
        log_path = args.compilation / 'log' / (Path(filename).stem + '.dxc.txt')
        log = log_path.read_text(encoding='utf-8') if log_path.exists() else ''
        missing = sorted(set(int(x) for x in re.findall(
            r"error: use of undeclared identifier 's(\d+)_(?:Texture\w+|Sampler)DescriptorIndex'", log)))
        uncovered = [x for x in missing if not any(r['register'] <= x < r['register'] + r['count'] for r in ranges)]
        all_array_tails = bool(missing) and all(any(r['register'] < x < r['register'] + r['count'] for r in ranges) for x in missing)
        totals['failed_shaders'] += 1
        totals['all_missing_registers_are_array_tails'] += all_array_tails
        totals['shaders_with_uncovered_registers'] += bool(uncovered)
        rows.append(dict(file=filename, container_sha256=sha(data), samplers=ranges,
                         missing_registers=missing, uncovered_registers=uncovered,
                         all_missing_registers_are_array_tails=all_array_tails))
    report = dict(evidence_kind='local-container-metadata-and-dxc-diagnostics',
                  compilation_report_sha256=sha(compile_bytes), tool_sha256=sha(Path(__file__).read_bytes()),
                  summary=dict(totals), results=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report['summary']))


if __name__ == '__main__':
    main()
