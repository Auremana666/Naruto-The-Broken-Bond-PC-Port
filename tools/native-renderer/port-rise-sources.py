"""Rewrite the imported Rise renderer sources for Broken Bond, in place.

Applies tools/native-renderer/rise-to-broken-bond.json to src/gpu:
  - generated function names (sub_XXXXXXXX) and function/LR/data address
    literals, in code and comments;
  - device offsets (decimal literals in the shifted ranges) through the
    piecewise segments and their exceptions;
  - the device clear/allocation sizes and the app's names.
Unported Rise functions are left as they are and listed; their hooks are
removed by hand. The rewrite runs once: it refuses a tree that no longer
contains the Rise device creation hook.
"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

SUB = re.compile(r'(?<![0-9A-Za-z])sub_([0-9A-F]{8})\b')  # Also __imp__sub_X.
HEX = re.compile(r'\b0x(8[23][0-9A-Fa-f]{6})(?=[uUlL]*\b)')  # Also with a u/ul/ull suffix.
BARE = re.compile(r'(?<![0-9A-Za-z_])(8[23][0-9A-F]{6})(?![0-9A-Za-z_])')
DECIMAL = re.compile(r'(?<![0-9A-Za-z_.])(\d{5})(u?)(?![0-9A-Za-z_]|\.\d)')  # A period may end a sentence.
NAMES = [
    ('[narutorise]', '[broken_bond]'),
    ('"narutorise/', '"broken_bond/'),
    ('narutorise.exe', 'broken_bond.exe'),
    ('narutorise.toml', 'broken_bond.toml'),
    ('L"narutorise"', 'L"broken_bond"'),
    ('NarutoriseApp', 'BrokenBondApp'),
    ('NARUTORISE_NATIVE_RENDERER', 'BROKENBOND_NATIVE_RENDERER'),
]


def load(path):
    m = json.load(open(path))
    functions = {k: v['bb'] for k, v in m['functions'].items()}
    addresses = {k: v['bb'] for k, v in m['addresses'].items() if v['bb']}
    seg = m['device_offsets']
    return functions, set(m['unported_functions']), addresses, seg


def offset(value, seg):
    if str(value) in seg['exceptions']:
        return seg['exceptions'][str(value)]
    for lo, hi, delta in seg['segments']:
        if lo <= value <= hi:
            return value + delta
    return None


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--mapping', default='tools/native-renderer/rise-to-broken-bond.json')
    p.add_argument('--sources', default='src/gpu')
    p.add_argument('--dry-run', action='store_true')
    a = p.parse_args()
    functions, unported, addresses, seg = load(a.mapping)
    root = Path(a.sources)
    marker = (root / 'd3d/device.cpp').read_text(encoding='utf-8')
    if 'sub_8213ECE8' not in marker:
        sys.exit('The sources no longer contain the Rise device creation hook; refusing to rewrite twice.')
    lo_shift = min(s[0] for s in seg['segments'] if s[2])
    hi_shift = max(s[1] for s in seg['segments'])
    stats, left = Counter(), Counter()
    for path in sorted(root.rglob('*')):
        if path.suffix not in ('.cpp', '.h', '.inc') or path.name.endswith('_ps.h') or path.name.endswith('_cs.h'):
            continue
        raw = path.read_bytes()
        text = raw.decode('utf-8')
        original = text

        def sub_name(m):
            k = m.group(1)
            if k in functions:
                stats['function names'] += 1
                return 'sub_' + functions[k]
            left['sub_' + k + (' (unported)' if k in unported else '')] += 1
            return m.group(0)

        def hex_literal(m):
            k = m.group(1).upper()
            v = functions.get(k) or addresses.get(k)
            if v:
                stats['hex literals'] += 1
                return '0x' + v
            left['0x' + k] += 1
            return m.group(0)

        def bare(m):
            k = m.group(1)
            v = functions.get(k) or addresses.get(k)
            if v:
                stats['bare addresses'] += 1
                return v
            return m.group(0)

        def decimal(m):
            value = int(m.group(1))
            if value == 20000 or value < lo_shift or value > hi_shift:
                return m.group(0)
            new = offset(value, seg)
            if new is None:
                left[f'offset {value}'] += 1
                return m.group(0)
            stats['device offsets'] += 1
            return f'{new}{m.group(2)}'

        text = SUB.sub(sub_name, text)
        text = HEX.sub(hex_literal, text)
        text = BARE.sub(bare, text)
        text = DECIMAL.sub(decimal, text)
        text = text.replace('kDeviceBytes = 22400', 'kDeviceBytes = 24064')
        text = text.replace('dev + 0x5800', 'dev + 0x5E80').replace('0x30F8', '0x3100')
        for old, new in NAMES:
            stats['names'] += text.count(old)
            text = text.replace(old, new)
        if text != original:
            stats['files'] += 1
            if not a.dry_run:
                path.write_bytes(text.encode('utf-8'))
    for k, v in sorted(stats.items()):
        print(f'{k}: {v}')
    if left:
        print('left unchanged:')
        for k, v in sorted(left.items()):
            print(f'  {k}: {v}')


if __name__ == '__main__':
    main()
