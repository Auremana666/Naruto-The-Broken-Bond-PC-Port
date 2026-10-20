"""Boundary checks for the measured SDB v4 layout, using synthetic data only."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location('extractor', Path(__file__).with_name('extract-sdb-containers.py'))
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


def fixture():
    blob = bytearray(84)
    struct.pack_into('>9I', blob, 0, 0x102A1101, 80, 4, 0, 36, 36, 40, 0, 0)
    struct.pack_into('>2I', blob, 40, 0, 4)
    packed = zlib.compress(blob)
    data = bytearray(24 + 0x4000 * 44)
    struct.pack_into('>6I', data, 0, extractor.MAGIC, extractor.BROKEN_BOND_VERSION, 0, 1, 24, 24)
    struct.pack_into('>3I', data, 24 + 32, len(data), len(packed), len(blob))
    data.extend(packed)
    return data, bytes(blob)


class ExtractionTests(unittest.TestCase):
    def test_decodes_container_and_records_provenance(self):
        data, blob = fixture()
        entries = list(extractor.read_containers(data))
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0][1], blob)
        self.assertEqual(entries[0][2]['stage'], 'vs')
        self.assertEqual(entries[0][3]['record_offset'], '0x18')

    def test_rejects_truncated_stream(self):
        data, _ = fixture()
        packed_size = struct.unpack_from('>I', data, 24 + 36)[0]
        struct.pack_into('>I', data, 24 + 36, packed_size - 1)
        with self.assertRaises(ValueError):
            list(extractor.read_containers(data[:-1]))

    def test_rejects_truncated_file(self):
        data, _ = fixture()
        with self.assertRaises(ValueError):
            list(extractor.read_containers(data[:-1]))

    def test_rejects_false_decoded_size(self):
        data, _ = fixture()
        struct.pack_into('>I', data, 24 + 40, 83)
        with self.assertRaises(ValueError):
            list(extractor.read_containers(data))

    def test_rejects_overlapping_payload(self):
        data, _ = fixture()
        struct.pack_into('>I', data, 24 + 32, 24)
        with self.assertRaises(ValueError):
            list(extractor.read_containers(data))

    def test_rejects_trailing_bytes(self):
        data, _ = fixture()
        with self.assertRaises(ValueError):
            list(extractor.read_containers(data + b'x'))

    def test_rejects_unknown_version(self):
        data, _ = fixture()
        struct.pack_into('>I', data, 4, 0x50001)
        with self.assertRaises(ValueError):
            list(extractor.read_containers(data))

    def test_rejects_nonzero_unused_record(self):
        data, _ = fixture()
        data[24 + 44] = 1
        with self.assertRaises(ValueError):
            list(extractor.read_containers(data))

    def test_legacy_scan_still_validates_container(self):
        _, blob = fixture()
        data = struct.pack('>6I', extractor.MAGIC, extractor.RISE_VERSION, 0, 1, 24, 24) + blob
        self.assertEqual(list(extractor.read_containers(data))[0][1], blob)


if __name__ == '__main__':
    unittest.main()
