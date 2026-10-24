import hashlib, sys
V3 = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/"
BE = "/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash/"
REL = V3 + "dualboot/linux-v2026.09.20/bootloader/bootloader/bootloader.bin"
files = {
 "dual": V3 + "images/korvo1-dualboot/bootloader.bin",
 "sa": V3 + "images/korvo1-standalone/bootloader.bin",
 "dos": V3 + "images/dosboard/bootloader.bin",
 "be5b725": BE + "bootloader.bin",
 "release": REL,
}
d = {k: open(v, "rb").read() for k, v in files.items()}
for k, b in d.items():
    print(k, len(b), hashlib.sha256(b).hexdigest()[:16], "head256", hashlib.sha256(b[:256]).hexdigest())
names = list(d)
for i in range(len(names)):
    for j in range(i+1, len(names)):
        a, b = d[names[i]], d[names[j]]
        n = min(len(a), len(b))
        first = next((k for k in range(n) if a[k] != b[k]), None)
        print(names[i], names[j], "first differing byte:", first)
print("ff256", hashlib.sha256(b"\xff"*256).hexdigest())
for k, b in d.items():
    print(k, "header:", b[:24].hex())
