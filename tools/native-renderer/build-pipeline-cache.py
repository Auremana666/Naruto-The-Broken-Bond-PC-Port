"""6.7 (A14): the list of precompiled pipelines, as the UnleashedRecomp's pipeline_state_cache.h.

The renderer writes the pipelines a boot created (pipelines.json in REX_NATIVE_FRAME_DIR,
since 6.5.6), each with the stable identities of its key: shader names of the cache, layout
id and elements, and the 21 state words. This tool merges those lists, in the order given
(the START first, so it compiles first), and writes pipeline_cache.inc,
which app/src/gpu/shaders/pipelines.cpp includes when present; the dispatcher pool compiles
it at the first draw. Like the shader cache, the list derives from the game and is git-ignored.

Checked for every entry: the layout id is the FNV-1a of the elements and the key the FNV-1a
of (VS name, "|", PS name, layout id, state words), as the draw layer and the engine compute
them; one key with two different descriptions stops the tool. Entries of the START policy
(names with ':') are left out: their DXIL comes from the policy files, not from the cache.

Stage 9 (increment254): a source may also be a pipeline_cache.inc this tool generated, so a
list keeps its entries after the boots it came from are deleted (the cleanup of out/ removed the
eleven sources of the 6.7 list); its entries go through the same checks.

Usage: python -I build-pipeline-cache.py <output dir> <pipelines.json | pipeline_cache.inc>...
Writes pipeline_cache.inc and pipeline_cache.json (sources
with SHA-256, counts, identity) into the output dir.
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

SEMANTICS = {'POSITION', 'BLENDWEIGHT', 'BLENDINDICES', 'NORMAL', 'PSIZE', 'TEXCOORD', 'TANGENT', 'BINORMAL',
             'TESSFACTOR', 'POSITIONT', 'COLOR', 'FOG', 'DEPTH', 'SAMPLE'}


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


BASIS = 0xCBF29CE484222325


def layout_id(elements):
    h = BASIS
    for semantic, index, fmt, slot, offset, step in elements:
        h = fnv(h, semantic.encode('ascii'))
        h = fnv(h, struct.pack('<4I', index, fmt, slot, offset))
        if step:  # Per-instance elements only, as the engine's ElementsId.
            h = fnv(h, struct.pack('<I', step))
    return h


def element_of(e):
    """(semantic, index, format, slot, offset, instance step); the step is written only when not 0."""
    assert len(e) in (5, 6), e
    return tuple(e[:5]) + (int(e[5]) if len(e) == 6 else 0,)


def key_of(vs, ps, layout, state):
    h = fnv(BASIS, vs.encode('ascii'))
    h = fnv(h, b'|')
    h = fnv(h, ps.encode('ascii'))
    h = fnv(h, struct.pack('<Q', layout))
    return fnv(h, struct.pack('<21I', *state))


def entries_of_inc(raw):
    """The entries of a generated pipeline_cache.inc, as the records of a pipelines.json."""
    text = raw.decode('ascii')
    elements = [(s, int(i), int(f), int(sl), int(o), int(st or 0)) for s, i, f, sl, o, st in
                re.findall(r'\{"([A-Z]+)", (\d+), (\d+), (\d+), (\d+)(?:, (\d+))?\},', text)]
    out = []
    for vs, ps, key, layout, first, count, state in re.findall(
            r'\{"([^"]+)", "([^"]+)", 0x([0-9a-f]{16})ull, 0x([0-9a-f]{16})ull, (\d+), (\d+), \{([0-9, ]+)\}\},', text):
        first, count = int(first), int(count)
        out.append({'key': key, 'vs': vs, 'ps': ps, 'layout': layout,
                    'elements': [list(e) for e in elements[first:first + count]],
                    'state': [int(w) for w in state.split(', ')]})
    header = re.search(r'// (\d+) pipelines, (\d+) layouts', text)
    assert header and int(header.group(1)) == len(out), 'inc entry count'
    return out


def main():
    out_dir = Path(sys.argv[1])
    sources = [Path(p) for p in sys.argv[2:]]
    if not sources:
        sys.exit(__doc__)
    entries, by_key, skipped, repeated = [], {}, 0, 0
    source_records = []
    for path in sources:
        raw = path.read_bytes()
        source_records.append({'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest(),
                               'pipelines': 0})
        for p in entries_of_inc(raw) if path.suffix == '.inc' else json.loads(raw):
            source_records[-1]['pipelines'] += 1
            vs, ps = p['vs'], p['ps']
            if ':' in vs or ':' in ps:
                skipped += 1
                continue
            elements = [element_of(e) for e in p['elements']]
            state = p['state']
            assert len(state) == 21 and all(0 <= w < 2 ** 32 for w in state), (path, p['key'])
            assert elements and all(e[0] in SEMANTICS for e in elements), (path, p['key'])
            layout = int(p['layout'], 16)
            key = int(p['key'], 16)
            if layout_id(elements) != layout:
                sys.exit(f'{path}: layout id {p["layout"]} is not the FNV of its elements')
            if key_of(vs, ps, layout, state) != key:
                sys.exit(f'{path}: key {p["key"]} is not the FNV of its identities')
            description = (vs, ps, layout, tuple(elements), tuple(state))
            if key in by_key:
                if by_key[key] != description:
                    sys.exit(f'{path}: key {p["key"]} with two descriptions')
                repeated += 1
                continue
            by_key[key] = description
            entries.append((key,) + description)
    layouts = {}
    element_lines, entry_lines = [], []
    for key, vs, ps, layout, elements, state in entries:
        if layout not in layouts:
            layouts[layout] = (len(element_lines), len(elements))
            element_lines += [f'    {{"{s}", {i}, {f}, {sl}, {o}{f", {st}" if st else ""}}},'
                              for s, i, f, sl, o, st in elements]
        first, count = layouts[layout]
        entry_lines.append(f'    {{"{vs}", "{ps}", 0x{key:016x}ull, 0x{layout:016x}ull, {first}, {count}, '
                           f'{{{", ".join(str(w) for w in state)}}}}},')
    body = '\n'.join(element_lines + ['--'] + entry_lines)
    identity = hashlib.sha256(body.encode('ascii')).hexdigest()
    inc = ['// Generated by tools/native-renderer/build-pipeline-cache.py (6.7, A14); do not edit.',
           f'// {len(entries)} pipelines, {len(layouts)} layouts, from {len(sources)} pipelines.json.',
           f'constexpr const char* kPipelineCacheIdentity = "{identity}";',
           'constexpr PipelineCacheElement kPipelineCacheElements[] = {', *element_lines, '};',
           'constexpr PipelineCacheEntry kPipelineCacheEntries[] = {', *entry_lines, '};', '']
    out_dir.mkdir(parents=True, exist_ok=True)
    inc_path = out_dir / 'pipeline_cache.inc'
    inc_path.write_bytes('\r\n'.join(inc).encode('ascii'))
    record = {'schema': 'native-pipeline-cache-v1', 'identity': identity, 'pipelines': len(entries),
              'layouts': len(layouts), 'depth_only': sum(1 for e in entries if e[2] == 'depth-only'),
              'spec': sum(1 for e in entries if '+spec' in e[2]), 'policy_skipped': skipped,
              'repeated': repeated, 'sources': source_records,
              'inc': {'path': str(inc_path), 'sha256': hashlib.sha256(inc_path.read_bytes()).hexdigest()},
              'tool': {'path': str(Path(__file__).resolve()),
                       'sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}}
    (out_dir / 'pipeline_cache.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('identity', 'pipelines', 'layouts', 'depth_only', 'spec',
                                             'policy_skipped', 'repeated')}))


if __name__ == '__main__':
    main()
