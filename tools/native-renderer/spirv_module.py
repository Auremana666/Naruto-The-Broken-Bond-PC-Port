"""The interface of a SPIR-V module, as the shader cache records it.

parse(data) reads the binary module and returns its capabilities and extensions,
the entry point, the stage inputs and outputs (by location or built-in, with
their types and interpolation), the specialization constants (SpecId and
default), the descriptor bindings (set, binding, type; a storage image with its
format, a uniform buffer with its bytes) and the push constant block (the
offset of its first member and its end, push_constant_bytes). Only the
declarations; the module is checked by spirv-val, not here. Unknown enumerants
keep their number.
"""
import struct

MAGIC = 0x07230203

CAPABILITIES = {
    0: 'Matrix', 1: 'Shader', 9: 'Float16', 10: 'Float64', 11: 'Int64', 22: 'Int16', 25: 'ImageGatherExtended',
    27: 'StorageImageMultisample', 28: 'UniformBufferArrayDynamicIndexing',
    29: 'SampledImageArrayDynamicIndexing', 30: 'StorageBufferArrayDynamicIndexing',
    31: 'StorageImageArrayDynamicIndexing', 32: 'ClipDistance', 33: 'CullDistance', 34: 'ImageCubeArray',
    35: 'SampleRateShading', 39: 'Int8', 42: 'MinLod', 43: 'Sampled1D', 44: 'Image1D', 45: 'SampledCubeArray',
    46: 'SampledBuffer', 49: 'StorageImageExtendedFormats', 50: 'ImageQuery', 51: 'DerivativeControl',
    52: 'InterpolationFunction', 55: 'StorageImageReadWithoutFormat', 56: 'StorageImageWriteWithoutFormat',
    5301: 'ShaderNonUniform', 5302: 'RuntimeDescriptorArray', 5306: 'SampledImageArrayNonUniformIndexing',
    5347: 'PhysicalStorageBufferAddresses'}
EXECUTION_MODELS = {0: 'vertex', 4: 'fragment', 5: 'compute'}
STORAGE_CLASSES = {0: 'uniform-constant', 1: 'input', 2: 'uniform', 3: 'output', 9: 'push-constant',
                   12: 'storage-buffer', 5349: 'physical-storage-buffer'}
BUILTINS = {0: 'Position', 1: 'PointSize', 3: 'ClipDistance', 4: 'CullDistance', 15: 'FragCoord', 17: 'FrontFacing',
            18: 'SampleId', 20: 'SampleMask', 22: 'FragDepth', 42: 'VertexIndex', 43: 'InstanceIndex'}
INTERPOLATION = {13: 'noperspective', 14: 'flat', 16: 'centroid', 17: 'sample'}
DIMENSIONS = {0: '1D', 1: '2D', 2: '3D', 3: 'Cube', 4: 'Rect', 5: 'Buffer', 6: 'SubpassData'}
IMAGE_FORMATS = {0: 'Unknown', 1: 'Rgba32f', 2: 'Rgba16f', 3: 'R32f', 4: 'Rgba8', 10: 'Rgba16', 11: 'Rgb10A2'}

# Opcodes and decorations read here.
OP_EXTENSION, OP_ENTRY_POINT, OP_CAPABILITY = 10, 15, 17
OP_TYPE_BOOL, OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR, OP_TYPE_MATRIX = 20, 21, 22, 23, 24
OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY = 25, 26, 27, 28, 29
OP_TYPE_STRUCT, OP_TYPE_POINTER, OP_CONSTANT = 30, 32, 43
OP_SPEC_CONSTANT_TRUE, OP_SPEC_CONSTANT_FALSE, OP_SPEC_CONSTANT = 48, 49, 50
OP_VARIABLE, OP_DECORATE, OP_MEMBER_DECORATE = 59, 71, 72
DEC_SPEC_ID, DEC_ARRAY_STRIDE, DEC_BUILTIN, DEC_LOCATION = 1, 6, 11, 30
DEC_COMPONENT, DEC_BINDING, DEC_DESCRIPTOR_SET, DEC_OFFSET = 31, 33, 34, 35


def _string(words):
    raw = b''.join(struct.pack('<I', w) for w in words)
    return raw.split(b'\0', 1)[0].decode('utf-8')


def _string_words(words):
    """The number of words a literal string takes (its terminating zero included)."""
    for i, w in enumerate(words):
        if (w >> 24) == 0 or (w & 0xFF) == 0 or ((w >> 8) & 0xFF) == 0 or ((w >> 16) & 0xFF) == 0:
            return i + 1
    return len(words)


def parse(data: bytes) -> dict:
    if len(data) < 20 or len(data) % 4:
        raise ValueError('spirv-size')
    words = struct.unpack(f'<{len(data) // 4}I', data)
    if words[0] != MAGIC:
        raise ValueError('spirv-magic')
    capabilities, extensions, entry_points = [], [], []
    types, constants, decorations, member_offsets, variables, spec = {}, {}, {}, {}, [], {}
    i = 5
    while i < len(words):
        count, opcode = words[i] >> 16, words[i] & 0xFFFF
        if not count:
            raise ValueError('spirv-instruction')
        op = words[i + 1:i + count]
        i += count
        if opcode == OP_CAPABILITY:
            capabilities.append(CAPABILITIES.get(op[0], f'Capability{op[0]}'))
        elif opcode == OP_EXTENSION:
            extensions.append(_string(op))
        elif opcode == OP_ENTRY_POINT:
            name_words = _string_words(op[2:])
            entry_points.append(dict(model=EXECUTION_MODELS.get(op[0], op[0]), name=_string(op[2:]),
                                     interface=list(op[2 + name_words:])))
        elif opcode == OP_TYPE_BOOL:
            types[op[0]] = ('bool',)
        elif opcode == OP_TYPE_INT:
            types[op[0]] = ('int', op[1], op[2])
        elif opcode == OP_TYPE_FLOAT:
            types[op[0]] = ('float', op[1])
        elif opcode in (OP_TYPE_VECTOR, OP_TYPE_MATRIX):
            types[op[0]] = ('vector' if opcode == OP_TYPE_VECTOR else 'matrix', op[1], op[2])
        elif opcode == OP_TYPE_IMAGE:
            # Sampled type, dim, arrayed, multisampled, sampled (2: a storage image), format.
            types[op[0]] = ('image', op[1], op[2], op[4], op[5], op[6], op[7])
        elif opcode == OP_TYPE_SAMPLER:
            types[op[0]] = ('sampler',)
        elif opcode == OP_TYPE_SAMPLED_IMAGE:
            types[op[0]] = ('sampled-image', op[1])
        elif opcode == OP_TYPE_ARRAY:
            types[op[0]] = ('array', op[1], op[2])
        elif opcode == OP_TYPE_RUNTIME_ARRAY:
            types[op[0]] = ('runtime-array', op[1])
        elif opcode == OP_TYPE_STRUCT:
            types[op[0]] = ('struct', *op[1:])
        elif opcode == OP_TYPE_POINTER:
            types[op[0]] = ('pointer', op[1], op[2])
        elif opcode == OP_CONSTANT:
            constants[op[1]] = op[2]
        elif opcode in (OP_SPEC_CONSTANT, OP_SPEC_CONSTANT_TRUE, OP_SPEC_CONSTANT_FALSE):
            spec[op[1]] = op[2] if opcode == OP_SPEC_CONSTANT else int(opcode == OP_SPEC_CONSTANT_TRUE)
        elif opcode == OP_VARIABLE:
            variables.append((op[1], op[0], op[2]))  # id, pointer type, storage class
        elif opcode == OP_DECORATE:
            decorations.setdefault(op[0], {})[op[1]] = op[2] if len(op) > 2 else True
        elif opcode == OP_MEMBER_DECORATE and op[2] == DEC_OFFSET:
            member_offsets.setdefault(op[0], {})[op[1]] = op[3]

    def describe(t):
        kind = types.get(t, ('?',))
        if kind[0] == 'bool':
            return 'bool'
        if kind[0] == 'int':
            return ('int' if kind[2] else 'uint') + ('' if kind[1] == 32 else str(kind[1]))
        if kind[0] == 'float':
            return {16: 'half', 32: 'float', 64: 'double'}.get(kind[1], f'float{kind[1]}')
        if kind[0] == 'vector':
            return f'{describe(kind[1])}{kind[2]}'
        if kind[0] == 'matrix':
            return f'{describe(kind[1])}x{kind[2]}'
        if kind[0] == 'array':
            return f'{describe(kind[1])}[{constants.get(kind[2], "?")}]'
        if kind[0] == 'runtime-array':
            return f'{describe(kind[1])}[]'
        if kind[0] == 'image':
            storage = 'storage-' if kind[5] == 2 else ''
            image = f'{storage}image{DIMENSIONS.get(kind[2], kind[2])}{"MS" if kind[4] else ""}' \
                    f'{"Array" if kind[3] else ""}<{describe(kind[1])}>'
            return f'{image}:{IMAGE_FORMATS.get(kind[6], kind[6])}' if storage else image
        if kind[0] == 'sampler':
            return 'sampler'
        if kind[0] == 'sampled-image':
            return f'sampled-{describe(kind[1])}'
        if kind[0] == 'struct':
            return 'struct{' + ','.join(describe(m) for m in kind[1:]) + '}'
        if kind[0] == 'pointer':
            return f'pointer<{STORAGE_CLASSES.get(kind[1], kind[1])}>'
        return '?'

    def size(t):
        kind = types.get(t, ('?',))
        if kind[0] in ('int', 'float'):
            return kind[1] // 8
        if kind[0] == 'bool':
            return 4
        if kind[0] in ('vector', 'matrix'):
            return size(kind[1]) * kind[2]
        if kind[0] == 'array':
            stride = decorations.get(t, {}).get(DEC_ARRAY_STRIDE) or size(kind[1])
            return stride * constants.get(kind[2], 0)
        if kind[0] == 'struct':
            offsets = member_offsets.get(t, {})
            return max((offsets.get(m, 0) + size(member) for m, member in enumerate(kind[1:])), default=0)
        if kind[0] == 'pointer':
            return 8  # A physical storage buffer address.
        return 0

    stage = dict(inputs=[], outputs=[])
    bindings, push_constant_offset, push_constant_bytes = [], None, 0
    for var, pointer, storage in variables:
        pointee = types.get(pointer, ('?', None, None))[2]
        decorated = decorations.get(var, {})
        if storage in (1, 3):
            entry = dict(type=describe(pointee))
            if DEC_BUILTIN in decorated:
                entry['builtin'] = BUILTINS.get(decorated[DEC_BUILTIN], decorated[DEC_BUILTIN])
            if DEC_LOCATION in decorated:
                entry['location'] = decorated[DEC_LOCATION]
            if DEC_COMPONENT in decorated:
                entry['component'] = decorated[DEC_COMPONENT]
            for decoration, name in INTERPOLATION.items():
                if decoration in decorated:
                    entry['interpolation'] = name
            stage['inputs' if storage == 1 else 'outputs'].append(entry)
        elif storage in (0, 2, 12):
            bindings.append(dict(set=decorated.get(DEC_DESCRIPTOR_SET), binding=decorated.get(DEC_BINDING),
                                 storage=STORAGE_CLASSES[storage], type=describe(pointee)))
            if storage == 2:
                bindings[-1]['bytes'] = size(pointee)
        elif storage == 9:
            push_constant_bytes = max(push_constant_bytes, size(pointee))
            first = min(member_offsets.get(pointee, {}).values(), default=0)
            push_constant_offset = first if push_constant_offset is None else min(push_constant_offset, first)
    key = lambda e: (e.get('location', -1), str(e.get('builtin', '')))
    return dict(version=f'{(words[1] >> 16) & 0xFF}.{(words[1] >> 8) & 0xFF}', capabilities=sorted(capabilities),
                extensions=sorted(extensions),
                entry_points=[dict(model=e['model'], name=e['name']) for e in entry_points],
                inputs=sorted(stage['inputs'], key=key), outputs=sorted(stage['outputs'], key=key),
                spec_constants={str(decorations[c][DEC_SPEC_ID]): spec[c] for c in sorted(spec)
                                if DEC_SPEC_ID in decorations.get(c, {})},
                bindings=sorted(bindings, key=lambda b: (b['set'] is None, b['set'] or 0, b['binding'] or 0)),
                push_constant_offset=push_constant_offset or 0, push_constant_bytes=push_constant_bytes)
