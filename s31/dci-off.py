#!/usr/bin/env python3
"""
s31/dci-off.py WINDOWS_FOLDER HOLDING_FOLDER: for the ESP-Mosaico (MOSAICO.md),
and any Windows 95 with VMDisp9x's display driver.

Windows 95's OpenGL (OPENGL32.DLL) draws straight into the screen through DCI
when the display driver offers it.  With VMDisp9x 1.2025.0.119 that goes
wrong: OpenGL writes into memory that is not the screen, and 3D Pipes (and the
other OpenGL screensavers) stop with an "illegal operation" in OPENGL32.DLL or
KRNL386.EXE, or a fatal exception in KERNEL32.  The same happens under QEMU, so
it is not tiny386's doing.

This makes DCIMAN32.DLL's DCIOpenProvider() return NULL, "no DCI provider"
(xor eax,eax; ret), so that OpenGL, and anything else that would use DCI,
draws through GDI instead.  3D Pipes then runs.

Only Windows 95 OSR2's DCIMAN32.DLL (5,632 bytes, 24 August 1996) is changed;
the script checks its SHA-256 and the function's first bytes.  The original
goes into the holding folder.  The file keeps its date, so that Windows' own
check of its system files does not take it for an older copy.

Works on a folder of the C: drive's files, or on the board's C: drive itself
in USB disk mode (WINDOWS_FOLDER being that drive, such as E:\\).
"""
import hashlib, os, shutil, struct, sys

ORIG = "caafa17a2ea7bb23cf6f06bf63e39cc64e635c265ffb459642a800be2d5b3000"
DONE = "84910f398b52fbf5985a772a028a16accb419764cb16b985f4a78b9fc443201f"
PROLOGUE = bytes.fromhex("b112558bec")      # the original's first bytes
STUB = bytes.fromhex("31c0c3")              # xor eax,eax; ret


def export_offset(d, name):
    """file offset of an exported function of a PE file"""
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    exp_rva = struct.unpack_from("<I", d, pe + 24 + 96)[0]
    secs = []
    for i in range(nsec):
        s = pe + 24 + optsz + 40 * i
        vsz, va, rsz, raw = struct.unpack_from("<IIII", d, s + 8)
        secs.append((va, max(vsz, rsz), raw))

    def off(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return rva - va + raw
        raise ValueError("RVA %x outside the sections" % rva)

    e = off(exp_rva)
    _, nnames, afun, aname, aord = struct.unpack_from("<IIIII", d, e + 20)
    for i in range(nnames):
        nrva = struct.unpack_from("<I", d, off(aname) + 4 * i)[0]
        if bytes(d[off(nrva):off(nrva) + len(name) + 1]) == name + b"\0":
            o = struct.unpack_from("<H", d, off(aord) + 2 * i)[0]
            return off(struct.unpack_from("<I", d, off(afun) + 4 * o)[0])
    raise ValueError("no export %s" % name.decode())


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    win, hold = sys.argv[1], sys.argv[2]
    dll = os.path.join(win, "WINDOWS", "SYSTEM", "DCIMAN32.DLL")
    if not os.path.isfile(dll):
        sys.exit("dci-off: there is no %s" % dll)
    d = bytearray(open(dll, "rb").read())
    sha = hashlib.sha256(d).hexdigest()
    if sha == DONE:
        print("dci-off: %s has DCI off already" % dll)
        return
    if sha != ORIG:
        sys.exit("dci-off: %s is not Windows 95 OSR2's DCIMAN32.DLL (SHA-256 %s): "
                 "nothing changed" % (dll, sha))
    fo = export_offset(d, b"DCIOpenProvider")
    if d[fo:fo + len(PROLOGUE)] != PROLOGUE:
        sys.exit("dci-off: DCIOpenProvider does not start as expected: nothing changed")
    st = os.stat(dll)
    keep = os.path.join(hold, "WINDOWS", "SYSTEM", "DCIMAN32.DLL.before-dci-off")
    os.makedirs(os.path.dirname(keep), exist_ok=True)
    shutil.copy2(dll, keep)
    d[fo:fo + len(STUB)] = STUB
    if hashlib.sha256(d).hexdigest() != DONE:
        sys.exit("dci-off: the result is not the tested one: nothing changed")
    with open(dll, "r+b") as f:
        f.write(d)
    os.utime(dll, (st.st_atime, st.st_mtime))
    print("dci-off: %s: DCIOpenProvider now says no DCI; original in %s" % (dll, keep))


if __name__ == "__main__":
    main()
