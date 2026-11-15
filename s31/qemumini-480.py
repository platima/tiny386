# s31/qemumini-480.py WINDOWS_FOLDER HOLDING_FOLDER: for the ESP-Mosaico (MOSAICO.md).
# 1. QEMUMINI.DRV (VMDisp9x 1.2025.0.119, with or without qemumini-pointer's
#    byte): FixModeInfo's width test 640 -> 480, so 480x480 is allowed; the
#    original kept beside it in the holding folder.
# 2. MOSAICO.REG: the 480x480 mode in the driver's list, and 480x480 in 16 bits
#    as the mode Windows starts in.
import hashlib, os, shutil, sys
W, H = sys.argv[1], sys.argv[2]
drv = os.path.join(W, "WINDOWS", "SYSTEM", "QEMUMINI.DRV")
d = bytearray(open(drv, "rb").read())
sha = hashlib.sha256(d).hexdigest()
ok = {"0b3584b11420b3bc96652d9ae147df2188572e0d1b967a098ac8513eb97b713d": "as released",
      "e4c47e9aa983db1cebbeb09e625c53e71986e78868b2ff9094a22abfec0538e9": "with qemumini-pointer"}
AT = 0x2A31
if d[AT - 3:AT + 2] == bytes.fromhex("26813fe001"):
    print("QEMUMINI.DRV: 480 already")
else:
    if sha not in ok or d[AT - 3:AT + 2] != bytes.fromhex("26813f8002"):
        sys.exit("QEMUMINI.DRV is not VMDisp9x 1.2025.0.119's (sha %s): nothing changed" % sha)
    keep = os.path.join(H, "WINDOWS", "SYSTEM", "QEMUMINI.DRV.before-480")
    os.makedirs(os.path.dirname(keep), exist_ok=True)
    shutil.copy2(drv, keep)
    d[AT:AT + 2] = b"\xe0\x01"
    open(drv, "wb").write(d)
    print("QEMUMINI.DRV (%s): width test now 480; original in %s" % (ok[sha], keep))
reg = os.path.join(W, "MOSAICO.REG")
text = open(reg, newline="").read()
add = ("[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Class\\Display\\0000\\MODES\\16\\480,480]\r\n\r\n"
       "[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Class\\Display\\0000\\DEFAULT]\r\n"
       "\"Mode\"=\"16,480,480\"\r\n\r\n"
       "[HKEY_LOCAL_MACHINE\\Config\\0001\\Display\\Settings]\r\n"
       "\"Resolution\"=\"480,480\"\r\n\"BitsPerPixel\"=\"16\"\r\n\r\n")
if "480,480" not in text:
    open(reg, "w", newline="").write(text.rstrip("\r\n") + "\r\n\r\n" + add)
print(open(reg).read())
