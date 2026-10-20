"""Check presentation evidence rejection using a retained host capture, without GPU work."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_present', Path(__file__).with_name('run-native-present.py'))
present = importlib.util.module_from_spec(spec)
spec.loader.exec_module(present)


class EvidenceChecks(unittest.TestCase):
    evidence = None

    def setUp(self):
        self.run = json.loads((self.evidence / 'summary.json').read_text(encoding='utf-8'))['runs'][0]
        self.records = [json.loads(line) for line in
                        (self.evidence / '01-window-present.jsonl').read_text(encoding='utf-8').splitlines()]
        self.temporary = tempfile.TemporaryDirectory(prefix='native-present-check-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def check_records(self, records, capture=None):
        journal = self.directory / 'case.jsonl'
        journal.write_text(''.join(json.dumps(record) + '\n' for record in records), encoding='utf-8')
        return present.validate(journal, self.run['exit_code'], self.run['source_image'],
                                self.run['back_buffer'], self.run['window_image'],
                                capture or self.run['capture'], self.run['pid'])

    def evidence_for(self, kind):
        return next(record['evidence'] for record in self.records if record['kind'] == kind)

    def changed_image(self, suffix, byte_offset):
        data = bytearray((self.evidence / ('01-window-present' + suffix)).read_bytes())
        data[byte_offset] ^= 1
        path = self.directory / 'changed.bgra'
        path.write_bytes(data)
        return path

    def test_original_evidence_passes(self):
        self.check_records(self.records)
        present.inspect_output(self.evidence / '01-window-present.bgra')
        present.inspect_output(self.evidence / '01-window-present.window.bgra', window=True)

    def test_last_back_buffer_alpha_is_checked(self):
        with self.assertRaisesRegex(ValueError, '1 mismatched output channels'):
            present.inspect_output(self.changed_image('.bgra', -1))

    def test_last_window_pixel_is_checked(self):
        with self.assertRaisesRegex(ValueError, '1 mismatched output channels'):
            present.inspect_output(self.changed_image('.window.bgra', -2), window=True)

    def test_gdi_unused_byte_is_excluded(self):
        result = present.inspect_output(self.changed_image('.window.bgra', -1), window=True)
        self.assertFalse(result['alpha_checked'])

    def test_truncated_image_is_rejected(self):
        path = self.directory / 'short.bgra'
        path.write_bytes(b'\x00' * 4)
        with self.assertRaisesRegex(ValueError, 'Wrong output byte count'):
            present.inspect_output(path, window=True)

    def test_missing_terminal_verdict_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete or unexpected'):
            self.check_records(self.records[:-1])

    def test_skipped_present_is_rejected_even_with_ok_record(self):
        self.evidence_for('window-present')['presented'] = False
        with self.assertRaisesRegex(ValueError, 'Presentation not proved'):
            self.check_records(self.records)

    def test_capture_from_another_process_is_rejected(self):
        capture = copy.deepcopy(self.run['capture'])
        capture['process'] += 1
        with self.assertRaisesRegex(ValueError, 'Wrong window identity'):
            self.check_records(self.records, capture)

    def test_wrong_source_key_is_rejected(self):
        self.evidence_for('front-stage')['key'] += 1
        with self.assertRaisesRegex(ValueError, 'Wrong staged source'):
            self.check_records(self.records)

    def test_missing_after_present_completion_is_rejected(self):
        self.evidence_for('idle-before-release')['observed_completed'] = 4
        with self.assertRaisesRegex(ValueError, 'Release preceded'):
            self.check_records(self.records)

    def test_window_destroyed_before_shutdown_is_rejected(self):
        destroy = next(record for record in self.records if record['kind'] == 'window-destroy')
        self.records.remove(destroy)
        self.records.insert(17, destroy)
        with self.assertRaisesRegex(ValueError, 'Incomplete or unexpected'):
            self.check_records(self.records)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path, help='accepted run-native-present.py evidence directory')
    args = parser.parse_args()
    EvidenceChecks.evidence = args.evidence.resolve(strict=True)
    unittest.main(argv=[__file__], verbosity=2)
