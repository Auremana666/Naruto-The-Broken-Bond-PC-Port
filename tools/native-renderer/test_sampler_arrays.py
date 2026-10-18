"""Exercise the real translator with synthetic Xenos shaders and sampler tables.

Usage: python -B test_sampler_arrays.py --xenosrecomp <exe> --common <header> --dxc <exe>
No game assets are required. Tests cover declaration offsets, fetch resolution,
singleton compatibility and invalid ranges in the single-container CLI.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


def shader_fixture(samplers, fetch_register):
    # ConstantTableContainer follows the nine-word shader container header.
    table = bytearray(28 + 20 * len(samplers))
    struct.pack_into('>7I', table, 0, 28, 0, 0xFFFF0300, len(samplers), 28, 0, 0)
    for index, entry in enumerate(samplers):
        name, first, count, *kind = entry
        offset = len(table)
        table.extend(name.encode('ascii') + b'\0')
        struct.pack_into('>I4H2I', table, 28 + index * 20, offset, kind[0] if kind else 3, first, count, 0, 0, 0)
    data = bytearray(36) + struct.pack('>I', len(table) + 4) + table
    data.extend(bytes(-len(data) % 4))
    shader_offset = len(data)
    # One control-flow pair and one texture fetch, all authored for this test.
    # ExecEnd: address=1, count=1, fetch sequence=1; second CF is NOP.
    code = struct.pack('>6I', 1 | (1 << 12) | (1 << 16), 2 << 12, 0,
                       1 | (fetch_register << 20) | (4 << 26), 0x688, 1 << 14)
    data.extend(struct.pack('>8I', 0, len(code), 0, 0, 0, 0, 0, 1))
    virtual_size = len(data)
    struct.pack_into('>9I', data, 0, 0x102A1100, virtual_size, len(code), 0, 36, 0, shader_offset, 0, 0)
    return data + code


class SamplerArrayTests(unittest.TestCase):
    def translate(self, samplers, register, error=None):
        with tempfile.TemporaryDirectory(prefix='broken-bond-samplers-') as folder:
            root = Path(folder)
            source, output = root / 'synthetic.bin', root / 'shader.hlsl'
            source.write_bytes(shader_fixture(samplers, register))
            run = subprocess.run([str(ARGS.xenosrecomp), str(source), str(output), str(ARGS.common)],
                                 capture_output=True, text=True, timeout=30)
            if error:
                self.assertEqual(run.returncode, 1, run.stderr)
                self.assertIn(error, run.stderr)
                self.assertFalse(output.exists())
                return ''
            self.assertEqual(run.returncode, 0, run.stderr)
            return output.read_text(encoding='utf-8')

    def test_array_fetch_and_both_descriptor_layouts(self):
        text = self.translate([('g_Textures', 4, 3)], 6)
        self.assertIn('tfetch2D(g_Textures_element_2_Texture2DDescriptorIndex, '
                      'g_Textures_element_2_SamplerDescriptorIndex,', text)
        for kind, offset, slot in [('Texture2D', 24, 'c1.z'), ('Texture3D', 88, 'c5.z'),
                                   ('TextureCube', 152, 'c9.z'), ('Sampler', 216, 'c13.z')]:
            name = f'g_Textures_element_2_{kind}DescriptorIndex'
            self.assertIn(f'#define {name} vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {offset})', text)
            self.assertIn(f'uint {name} : packoffset({slot});', text)

    def test_singleton_keeps_original_name(self):
        text = self.translate([('g_Texture', 2, 1)], 2)
        self.assertNotIn('g_Texture_element_', text)
        self.assertIn('tfetch2D(g_Texture_Texture2DDescriptorIndex, g_Texture_SamplerDescriptorIndex,', text)
        self.assertIn('uint g_Texture_Texture2DDescriptorIndex : packoffset(c0.z);', text)

    def test_crosses_constant_vector_boundary(self):
        text = self.translate([('g_Textures', 3, 3)], 5)
        self.assertIn('uint g_Textures_Texture2DDescriptorIndex : packoffset(c0.w);', text)
        self.assertIn('uint g_Textures_element_1_Texture2DDescriptorIndex : packoffset(c1.x);', text)
        self.assertIn('uint g_Textures_element_2_SamplerDescriptorIndex : packoffset(c13.y);', text)

    def test_last_supported_register(self):
        text = self.translate([('g_Textures', 14, 2)], 15)
        self.assertIn('uint g_Textures_element_1_Texture2DDescriptorIndex : packoffset(c3.w);', text)
        self.assertIn('uint g_Textures_element_1_SamplerDescriptorIndex : packoffset(c15.w);', text)

    def test_adjacent_arrays_keep_distinct_names(self):
        text = self.translate([('g_A', 0, 2), ('g_B', 2, 2)], 3)
        self.assertIn('tfetch2D(g_B_element_1_Texture2DDescriptorIndex,', text)
        self.assertIn('uint g_A_element_1_Texture2DDescriptorIndex : packoffset(c0.y);', text)

    def test_rejects_overlap(self):
        self.translate([('g_A', 0, 3), ('g_B', 2, 1)], 2, 'Overlapping sampler register')

    def test_rejects_zero_length(self):
        self.translate([('g_A', 0, 0)], 0, 'invalid shared descriptor range')

    def test_rejects_range_overflow(self):
        for first, count in [(15, 2), (16, 1), (0, 65535), (65535, 2)]:
            with self.subTest(first=first, count=count):
                self.translate([('g_A', first, count)], 0, 'invalid shared descriptor range')

    def test_rejects_generated_name_collision(self):
        self.translate([('g_A', 0, 2), ('g_A_element_1', 4, 1)], 1, 'Sampler name collision')

    def test_rejects_duplicate_sampler_names(self):
        self.translate([('g_A', 0, 1), ('g_A', 1, 1)], 0, 'Sampler name collision')

    def test_rejects_descriptor_collision_with_float_constant(self):
        for suffix in ['Texture2D', 'Texture3D', 'TextureCube', 'Sampler']:
            with self.subTest(suffix=suffix):
                self.translate([('g_A', 0, 2), (f'g_A_element_1_{suffix}DescriptorIndex', 4, 1, 2)],
                               1, 'Sampler descriptor name collision')

    def test_directory_mode_reports_translation_failure(self):
        with tempfile.TemporaryDirectory(prefix='broken-bond-samplers-') as folder:
            root = Path(folder)
            inputs = root / 'inputs'
            inputs.mkdir()
            (inputs / 'invalid.bin').write_bytes(shader_fixture([('g_A', 15, 2)], 15))
            output = root / 'cache.cpp'
            run = subprocess.run([str(ARGS.xenosrecomp), str(inputs), str(output), str(ARGS.common)],
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 1, run.stderr)
            self.assertIn('Shader cache not written: 1 translation failure', run.stderr)
            self.assertFalse(output.exists())

    def test_array_shader_compiles_to_dxil_and_spirv(self):
        text = self.translate([('g_Textures', 4, 3)], 6)
        text += '\n#ifndef __spirv__\nuint g_SpecConstants() { return 0; }\n#endif\n'
        with tempfile.TemporaryDirectory(prefix='broken-bond-samplers-') as folder:
            root = Path(folder)
            source = root / 'array.hlsl'
            source.write_text(text, encoding='utf-8')
            for spirv in (False, True):
                with self.subTest(spirv=spirv):
                    output = root / ('array.spv' if spirv else 'array.dxil')
                    flags = ['-spirv', '-fvk-use-dx-layout'] if spirv else ['-Wno-ignored-attributes', '-Qstrip_reflect']
                    run = subprocess.run([str(ARGS.dxc), '-T', 'ps_6_0', '-HV', '2021', '-all-resources-bound',
                                          '-Qstrip_debug', *flags, '-Fo', str(output), str(source)],
                                         capture_output=True, text=True, timeout=30)
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertTrue(output.is_file() and output.stat().st_size > 0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xenosrecomp', type=Path, required=True)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--dxc', type=Path, required=True)
    ARGS = parser.parse_args()
    ARGS.xenosrecomp = ARGS.xenosrecomp.resolve()
    ARGS.common = ARGS.common.resolve()
    ARGS.dxc = ARGS.dxc.resolve()
    unittest.main(argv=['test_sampler_arrays.py'])
