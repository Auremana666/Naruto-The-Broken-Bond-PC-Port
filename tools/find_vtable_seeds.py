"""Finds code addresses stored in data sections (vtables, callback tables) that codegen did not register
as functions, where the previous instruction ends a function (blr, bctr, unconditional b, padding).

Usage: python find_vtable_seeds.py default.bin narutobb_register.cpp
Prints TOML seed lines.
"""
import re
import struct
import sys

import capstone

BASE = 0x82000000
CODE_START, CODE_END = 0x82120000, 0x82C7A2F8
# .rdata/.data and the BINK data section, from the PE headers.
DATA_RANGES = [(0x82000600, 0x820E987C), (0x82C80000, 0x833BC358), (0x833BC400, 0x833C0108)]


def main(image_path, register_path):
    img = open(image_path, "rb").read()
    known = {int(m, 16) for m in re.findall(r"SetFunction\(0x([0-9A-Fa-f]+)", open(register_path).read())}
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)

    def insn(address):
        word = img[address - BASE: address - BASE + 4]
        decoded = list(md.disasm(word, address))
        return (decoded[0].mnemonic if decoded else None), word

    candidates = {}
    for start, end in DATA_RANGES:
        for offset in range(start - BASE, end - BASE, 4):
            value = struct.unpack_from(">I", img, offset)[0]
            if not (CODE_START <= value < CODE_END) or value & 3 or value in known:
                continue
            mnemonic, _ = insn(value)
            if mnemonic is None:
                continue
            previous, word = insn(value - 4)
            if previous in ("blr", "bctr", "b") or word == b"\0\0\0\0":
                candidates.setdefault(value, BASE + offset)

    for address, ref in sorted(candidates.items()):
        print('"0x%08X" = {} # referenced from data at 0x%08X' % (address, ref))
    print("# %d candidates" % len(candidates), file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
