"""Summarize a native renderer journal (JSONL from --native_renderer_bootstrap_path).

Prints the record kinds, every stop/failure/census record and the last records,
omitting the high-volume module, last-error and wait records.

usage: python -I summarize-native-journal.py <journal.jsonl> [last-records]
"""
import collections
import json
import sys

NOISE = {'module', 'native-cpu-last-error-preserved', 'wait-before-original-sdk', 'wait-after-original-sdk'}


def main():
    path = sys.argv[1]
    tail = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    kinds = collections.Counter()
    rows = []
    for line in open(path, encoding='utf-8', errors='replace'):
        try:
            record = json.loads(line)
        except ValueError:
            rows.append({'kind': 'unparsed', 'text': line[:300]})
            continue
        kinds[record.get('kind')] += 1
        if record.get('kind') not in NOISE:
            rows.append(record)
    print('kinds:', dict(kinds.most_common()))
    for record in rows:
        kind = record.get('kind', '')
        if 'stop' in kind or 'fail' in kind or 'census' in kind:
            print('!!', json.dumps(record)[:1500])
    print('--- last records')
    for record in rows[-tail:]:
        print(json.dumps(record)[:700])


if __name__ == '__main__':
    main()
