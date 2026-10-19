"""Reads the running game's guest memory (Xbox 360 addresses) from outside the process.

The runtime maps guest memory at host 0x100000000 + guest address.
Usage: python guest_mem.py <pid> u32 <addr>            read a big-endian word
       python guest_mem.py <pid> dump <addr> <size> <file>
       python guest_mem.py <pid> chain <addr> <off> <off> ...   follow pointers
"""
import ctypes
import struct
import sys

GUEST_BASE = 0x100000000
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = ctypes.c_void_p
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = k32.ReadProcessMemory.argtypes


def open_process(pid):
    h = k32.OpenProcess(0x1F0FFF, False, pid)
    if not h:
        raise OSError(ctypes.get_last_error())
    return h


def read(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(GUEST_BASE + addr), buf, size, ctypes.byref(got)):
        raise OSError("read failed at %08X" % addr)
    return buf.raw[: got.value]


def write(h, addr, data):
    got = ctypes.c_size_t()
    if not k32.WriteProcessMemory(h, ctypes.c_void_p(GUEST_BASE + addr), data, len(data), ctypes.byref(got)):
        raise OSError("write failed at %08X" % addr)


def u32(h, addr):
    return struct.unpack(">I", read(h, addr, 4))[0]


if __name__ == "__main__":
    pid = int(sys.argv[1])
    h = open_process(pid)
    cmd = sys.argv[2]
    if cmd == "u32":
        print("%08X" % u32(h, int(sys.argv[3], 16)))
    elif cmd == "dump":
        open(sys.argv[5], "wb").write(read(h, int(sys.argv[3], 16), int(sys.argv[4], 16)))
    elif cmd == "chain":
        a = u32(h, int(sys.argv[3], 16))
        print("%08X" % a)
        for off in sys.argv[4:]:
            a = u32(h, a + int(off, 16))
            print("+%s -> %08X" % (off, a))
