"""Finds code addresses the game builds in registers (lis rX,hi then addi/ori rY,rX,lo) that codegen did
not register as functions, where the previous instruction ends a function (blr, bctr, unconditional b,
padding). These are callbacks passed by pointer, such as sort comparators, which otherwise crash with
"Call to invalid or unregistered function" the first time the game calls them.

Usage: python find_code_ptr_seeds.py default.bin narutobb_register.cpp
Prints TOML seed lines.
"""
import re
import struct
import sys

import capstone

BASE = 0x82000000
CODE_START, CODE_END = 0x82120000, 0x82C7A2F8


def main(image_path, register_path):
    img = open(image_path, "rb").read()
    known = {int(m, 16) for m in re.findall(r"SetFunction\(0x([0-9A-Fa-f]+)", open(register_path).read())}
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)

    def word_at(address):
        return struct.unpack_from(">I", img, address - BASE)[0]

    def mnemonic_at(address):
        decoded = list(md.disasm(img[address - BASE: address - BASE + 4], address))
        return decoded[0].mnemonic if decoded else None

    candidates = {}
    for pc in range(CODE_START, CODE_END, 4):
        w = word_at(pc)
        if w >> 26 != 15:  # addis (lis when rA = 0)
            continue
        rd, ra = (w >> 21) & 31, (w >> 16) & 31
        if ra != 0:
            continue
        hi = w & 0xFFFF
        # Look a few instructions ahead for addi/ori that completes the address from rd.
        for follow in range(pc + 4, min(pc + 4 * 12, CODE_END), 4):
            f = word_at(follow)
            op = f >> 26
            if op in (14, 24) and ((f >> 16) & 31) == rd:  # addi rY,rd,lo / ori rY,rd,lo
                lo = f & 0xFFFF
                if op == 14 and lo & 0x8000:
                    value = ((hi << 16) - 0x10000 + lo) & 0xFFFFFFFF
                else:
                    value = ((hi << 16) | lo) if op == 24 else ((hi << 16) + lo) & 0xFFFFFFFF
                if CODE_START <= value < CODE_END and not value & 3 and value not in known:
                    if mnemonic_at(value) is not None:
                        prev = mnemonic_at(value - 4)
                        if prev in ("blr", "bctr", "b") or word_at(value - 4) == 0:
                            candidates.setdefault(value, pc)
                break
            if ((f >> 21) & 31) == rd and op not in (14, 24):
                # rd overwritten by something else; stop following it.
                pass
    for address, ref in sorted(candidates.items()):
        print('"0x%08X" = {} # address built in code at 0x%08X' % (address, ref))
    print("# %d candidates" % len(candidates), file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
