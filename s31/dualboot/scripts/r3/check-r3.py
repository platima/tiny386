#!/usr/bin/env python3
# Round 3 of the Korvo-1 dual-boot checks (2026-10-21, second resumed run).
#
# Rechecks the dual-boot set as it is on disk now, after the v3.1 build and
# the review: the images the scripts point at, the hash pins and write lists
# inside flash-dualboot.bat, flash-standalone.bat, back-to-linux.bat and
# flash-paths.cmd, the commands in FLASH.txt, the two partition tables, the
# bootloader's size and config, and the Linux release's reach.
#
# Reads only.  Writes only under OUT.  No serial port, no board.
#
#   python3 -I check-r3.py OUT W V3WIN IDF_PATH
#     OUT    output folder (r3/out)
#     W      ~/tiny386-idf-scratch/dualboot (first-round build and inputs)
#     V3WIN  the v3 folder as Windows sees it, D:\...\tiny386-idf\v3
#     IDF_PATH
import difflib, hashlib, json, os, re, struct, subprocess, sys, zipfile

OUT, W, V3WIN, IDF = sys.argv[1:5]
os.makedirs(OUT, exist_ok=True)
GEN = os.path.join(IDF, "components/partition_table/gen_esp32part.py")
REL_ZIP = "/mnt/d/Repos/esp32-s31-linux-release/esp32-s31-linux-flash-v2026.09.20.zip"
BE_PKG = "/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash"
V2IMG = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v2/images"
TEST_OFF, TEST_SIZE, FLASH = 0xE20000, 0x1E0000, 0x1000000
BL_OFF, PT_OFF = 0x2000, 0x8000

rep = []
fails = []


def p(s=""):
    rep.append(s)


def ok(cond, what):
    p("  %s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def wsl(winpath):
    """D:\\a\\b -> /mnt/d/a/b, normalised."""
    s = winpath.replace("\\", "/")
    m = re.match(r"^([A-Za-z]):/(.*)$", s)
    if m:
        s = "/mnt/%s/%s" % (m.group(1).lower(), m.group(2))
    return os.path.normpath(s)


V3 = wsl(V3WIN)


def rd(path):
    with open(path, "rb") as f:
        return f.read()


def sha(path_or_bytes):
    b = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else rd(path_or_bytes)
    return hashlib.sha256(b).hexdigest()


def short(path):
    for pre, tag in ((V3 + "/", "v3/"), (BE_PKG + "/", "be5b725/"), (W + "/", "W/"),
                     (V2IMG + "/", "v2/images/")):
        if path.startswith(pre):
            return tag + path[len(pre):]
    return path


def run(args, **kw):
    r = subprocess.run(args, capture_output=True, text=True, **kw)
    return r.returncode, r.stdout + r.stderr


# ---------------------------------------------------------------- .bat parsing
def expand(s, env):
    """cmd-style %NAME% expansion against env, %~dp0 as the v3 folder."""
    s = s.replace("%~dp0", V3WIN.rstrip("\\") + "\\")
    for _ in range(6):
        s2 = re.sub(r"%(\w+)%", lambda m: env.get(m.group(1).upper(), m.group(0)), s)
        if s2 == s:
            break
        s = s2
    return s


def parse_sets(text, env):
    """Every 'set "N=V"' or 'set N=V' line in file order, expanded."""
    for line in text.splitlines():
        m = re.search(r'\bset\s+"(\w+)=([^"]*)"\s*$', line) or \
            re.search(r'\bset\s+(\w+)=(.*)$', line)
        if not m or line.lstrip().lstrip("@").lower().startswith("rem"):
            continue
        name, val = m.group(1).upper(), m.group(2)
        if "set /p" in line.lower():
            continue
        if re.search(r"if not defined\s+%s\b" % name, line, re.I) and name in env:
            continue
        env[name] = expand(val, env)
    return env


def labels(text):
    """{label: body} for a .bat file."""
    out, cur = {}, None
    for line in text.splitlines():
        m = re.match(r"^:(\w+)\s*$", line)
        if m:
            cur = m.group(1).lower()
            out[cur] = []
        elif cur:
            out[cur].append(line)
    return {k: "\n".join(v) for k, v in out.items()}


def pairs_quoted(s):
    return [(int(a, 16), wsl(f)) for a, f in re.findall(r'(0x[0-9A-Fa-f]+)\s+"([^"]+)"', s)]


paths_cmd = open(os.path.join(V3, "flash-paths.cmd"), encoding="latin-1").read()
bat_dual = open(os.path.join(V3, "flash-dualboot.bat"), encoding="latin-1").read()
bat_sa = open(os.path.join(V3, "flash-standalone.bat"), encoding="latin-1").read()
bat_lin = open(os.path.join(V3, "back-to-linux.bat"), encoding="latin-1").read()
flash_txt = open(os.path.join(V3, "FLASH.txt"), encoding="latin-1").read()

base_env = {"HERE": V3WIN.rstrip("\\") + "\\"}
parse_sets(paths_cmd, base_env)

p("### r3. The dual-boot set as it is on disk now (v3.1 images, scripts after the review)")
p("v3 folder: %s" % V3)
p()
p("== A. Files the scripts are given, and their SHA-256 against v3\\SHA256SUMS")
sums = {}
for line in open(os.path.join(V3, "SHA256SUMS")):
    h, f = line.split(None, 1)
    sums[f.strip().lstrip("*")] = h
for f in ("flash-dualboot.bat", "flash-standalone.bat", "back-to-linux.bat", "flash-paths.cmd", "FLASH.txt",
          "images/korvo1-dualboot/bootloader.bin", "images/korvo1-dualboot/partition-table.bin",
          "images/korvo1-dualboot/tiny386.bin", "images/korvo1-standalone/bootloader.bin",
          "images/korvo1-standalone/partition-table.bin", "images/korvo1-standalone/tiny386.bin"):
    h = sha(os.path.join(V3, f))
    ok(sums.get(f) == h, "%-45s %s, as v3\\SHA256SUMS lists it" % (f, h[:16]))
ds = {}
for line in open(os.path.join(V3, "dualboot/SHA256SUMS")):
    h, f = line.split(None, 1)
    ds[f.strip().lstrip("*").lstrip("./")] = h
for f in ("bootloader.bin", "partition-table.bin", "blank-0x9000-0x1ffff.bin", "partitions-korvo1-dualboot.csv"):
    ok(ds.get(f) == sha(os.path.join(V3, "dualboot", f)), "dualboot/%-35s as dualboot\\SHA256SUMS lists it" % f)
ok(rd(os.path.join(V3, "dualboot/bootloader.bin")) == rd(os.path.join(V3, "images/korvo1-dualboot/bootloader.bin"))
   == rd(os.path.join(W, "dual/build/bootloader/bootloader/bootloader.bin")),
   "dual-boot bootloader: dualboot\\, images\\korvo1-dualboot\\ and the WSL build output are the same bytes")
ok(rd(os.path.join(V3, "dualboot/partition-table.bin")) == rd(os.path.join(V3, "images/korvo1-dualboot/partition-table.bin"))
   == rd(os.path.join(W, "dual/build/bootloader/partition_table/partition-table.bin")),
   "dual-boot table: dualboot\\, images\\korvo1-dualboot\\ and the WSL build output are the same bytes")
ok(sha(os.path.join(V3, "images/korvo1-standalone/bootloader.bin")) == sha(os.path.join(V2IMG, "bootloader/bootloader.bin")),
   "standalone bootloader is v2's own (ec0a396b)")
ok(sha(os.path.join(V3, "images/korvo1-standalone/partition-table.bin")) == sha(os.path.join(V2IMG, "partition_table/partition-table.bin")),
   "standalone table is v2's own (afbe6410)")

# ------------------------------------------------------------- hash pins
p()
p("== B. Hash pins inside the scripts, resolved to the files they name")
env_dual = parse_sets(bat_dual, dict(base_env))
env_sa = parse_sets(bat_sa, dict(base_env))
env_lin = parse_sets(bat_lin, dict(base_env))
be_table = rd(os.path.join(BE_PKG, "partition-table.bin"))
rel_table = zipfile.ZipFile(REL_ZIP).read("bootloader/partition_table/partition-table.bin")
flashbin = rd(os.path.join(BE_PKG, "flash.bin"))
dual_table = rd(os.path.join(V3, "images/korvo1-dualboot/partition-table.bin"))
own_table = rd(os.path.join(V3, "images/korvo1-standalone/partition-table.bin"))
for name, env in (("flash-dualboot.bat", env_dual), ("flash-standalone.bat", env_sa), ("back-to-linux.bat", env_lin)):
    ok(env.get("PT_LINUX") == sha(be_table) == sha(rel_table) == sha(flashbin[0x8000:0x8C00]),
       "%s PT_LINUX = be5b725 table = release table = be5b725 flash.bin 0x8000..0x8BFF" % name)
    ok(env.get("PT_DUAL") == sha(dual_table), "%s PT_DUAL = the dual-boot table" % name)
    ok(env.get("PT_OWN") == sha(own_table), "%s PT_OWN = tiny386's own table" % name)
ok(len(dual_table) == len(be_table) == len(own_table) == 0xC00,
   "every table file is 0xC00 bytes, the length read-board reads at 0x8000")
ok(env_dual["BL_SHA"] == sha(wsl(env_dual["BL"])), "flash-dualboot.bat BL_SHA = %s" % short(wsl(env_dual["BL"])))
ok(env_dual["PT_SHA"] == sha(wsl(env_dual["PT"])), "flash-dualboot.bat PT_SHA = %s" % short(wsl(env_dual["PT"])))
ok(env_sa["SA_BL_SHA"] == sha(wsl(env_sa["SA_BOOTLOADER"])), "flash-standalone.bat SA_BL_SHA = %s" % short(wsl(env_sa["SA_BOOTLOADER"])))
ok(env_sa["SA_PT_SHA"] == sha(wsl(env_sa["SA_TABLE"])), "flash-standalone.bat SA_PT_SHA = %s" % short(wsl(env_sa["SA_TABLE"])))
lab = labels(bat_lin)
lin_sets = {}
for key in ("set_release_small", "set_release_full", "set_be5b725_small", "set_be5b725_full"):
    body = lab[key]
    for f, h in re.findall(r'call :check_hash "([^"]+)" (\w+)', body):
        path = wsl(expand(f, env_lin))
        ok(os.path.exists(path) and sha(path) == h, "back-to-linux.bat :%s pins %s" % (key, short(path)))
    m = re.search(r"^set WRITES=(.*)$", body, re.M)
    lin_sets[key] = pairs_quoted(expand(m.group(1), env_lin))

# ------------------------------------------------------------- write lists
p()
p("== C. What each script writes (offset, file, bytes, SHA-256), from the scripts themselves")
sets = {
    "dualboot-run1": pairs_quoted(expand(re.search(r"^set WRITES1=(.*)$", bat_dual, re.M).group(1), env_dual)),
    "dualboot-run2": pairs_quoted(expand(re.search(r"^set WRITES2=(.*)$", bat_dual, re.M).group(1), env_dual)),
    "standalone": pairs_quoted(expand(re.search(r"^set WRITES=(.*)$", bat_sa, re.M).group(1), env_sa)),
    "linux-release-small": lin_sets["set_release_small"],
    "linux-release-full": lin_sets["set_release_full"],
    "linux-be5b725-small": lin_sets["set_be5b725_small"],
    "linux-be5b725-full": lin_sets["set_be5b725_full"],
}
sets["dualboot-app"] = list(sets["dualboot-run1"])
for k, v in sets.items():
    p("  %s:" % k)
    for a, f in v:
        p("    0x%07x  %-62s %9d  %s" % (a, short(f), os.path.getsize(f), sha(f)[:16]))
ok([a for a, f in sets["dualboot-run1"]] == [TEST_OFF], "flash-dualboot.bat run 1 writes only 0xE20000")
ok([a for a, f in sets["dualboot-run2"]] == [BL_OFF, PT_OFF], "flash-dualboot.bat run 2 writes only 0x2000 and 0x8000")
ok(sets["dualboot-run1"][0][1] == wsl(base_env["TINY386_BIN"]),
   "the tiny386 image comes from one variable, TINY386_BIN in flash-paths.cmd (%s)" % short(wsl(base_env["TINY386_BIN"])))
ok(re.search(r"call :check_image \"%TINY386_BIN%\" %APP_MAX%", bat_dual) is not None and env_dual["APP_MAX"] == str(TEST_SIZE),
   "flash-dualboot.bat checks the image against APP_MAX = %s = 0x1E0000" % env_dual.get("APP_MAX"))
for name, txt in (("flash-dualboot.bat", bat_dual), ("flash-standalone.bat", bat_sa), ("back-to-linux.bat", bat_lin)):
    cmds = re.findall(r'set "CMD\d=([^"]*(?:"[^"]*"[^"]*)*)"\s*$', txt, re.M)
    ok(cmds and all("-c esp32s31 -p %PORT% -b 921600 -a hard-reset write-flash -fm keep -ff keep -fs keep" in c for c in cmds),
       "%s: every write command is '-c esp32s31 -p PORT -b 921600 -a hard-reset write-flash -fm keep -ff keep -fs keep'" % name)
    ok(re.search(r'set /p "ANS=.*"\s*\n\s*if /i not "%ANS%"=="Y" goto cancelled', txt) is not None,
       "%s: writes only after the answer Y" % name)
    first_write = min(txt.find("call %CMD1%"), len(txt))
    ok(txt.find("set /p") < first_write and txt.find("call :read_board") < txt.find("set /p"),
       "%s: reads the board, then asks, then writes (in that order in the file)" % name)
    ok("read-flash 0x8000 0xC00" in txt and "read-mac" in txt, "%s: the board read is read-mac and read-flash 0x8000 0xC00 only" % name)
    ok(not re.search(r"\b(erase-flash|erase-region|write-flash-status|burn|efuse)\b", txt, re.I),
       "%s: no erase-flash, erase-region, status-register or eFuse command" % name)
json.dump({k: [[a, f] for a, f in v] for k, v in sets.items()}, open(os.path.join(OUT, "write-lists.json"), "w"), indent=1)

p()
p("== D. FLASH.txt's commands by hand against the scripts' lists (run from the v3 folder)")
ft = []
for line in flash_txt.splitlines():
    if re.match(r"^\s*esptool -c esp32s31 -p COM\w+ -b 921600 write-flash -fm keep -ff keep -fs keep ", line):
        tail = line.split("-fs keep", 1)[1]
        ft.append([(int(a, 16), os.path.normpath(os.path.join(V3, f.replace("\\", "/"))))
                   for a, f in re.findall(r"(0x[0-9A-Fa-f]+)\s+(\S+)", tail)])
    elif "esptool" in line and "write-flash" in line:
        ok(False, "FLASH.txt line not in the expected form: " + line.strip())
p("  %d write-flash lines, all '-c esp32s31 -p COMx -b 921600 write-flash -fm keep -ff keep -fs keep'" % len(ft))
def same(a, b):
    return [x for x, _ in a] == [x for x, _ in b] and all(sha(f) == sha(g) for (_, f), (_, g) in zip(a, b))
ok(len(ft) >= 6, "FLASH.txt has the six command lines (1: two, 3, 4, 5, 6)")
ok(same(ft[0], sets["dualboot-run1"]) and same(ft[1], sets["dualboot-run2"]), "FLASH.txt 1 = flash-dualboot.bat runs 1 and 2, same order")
ok(same(ft[2], sets["dualboot-app"]), "FLASH.txt 3 = flash-dualboot.bat app")
ok(same(ft[3], sets["linux-be5b725-small"]), "FLASH.txt 4 = back-to-linux.bat small be5b725 (the default)")
ok(same(ft[4], sets["standalone"]), "FLASH.txt 5 = flash-standalone.bat")
ok([a for a, _ in ft[5]] == [BL_OFF, PT_OFF, 0x10000] and all(os.path.exists(f) for _, f in ft[5]),
   "FLASH.txt 6 (DOS board) writes 0x2000, 0x8000, 0x10000 from files that exist")

# ------------------------------------------------------------- tables
p()
p("== E. Partition tables")
ok(be_table == rel_table, "be5b725 and release v2026.09.20 tables are byte-identical (sha %s)" % sha(be_table)[:16])
for live, label in ((os.path.expanduser("~/esp32-s31-linux-main/bootloader/partitions.csv"), "be5b725 tree"),
                    (os.path.expanduser("~/esp32-s31-linux/bootloader/partitions.csv"), "fork tree")):
    if os.path.exists(live):
        ok(rd(live) == rd(os.path.join(W, "src/linux-be5b725/bootloader/partitions.csv")),
           "%s bootloader/partitions.csv (read now) = the copy the build used" % label)
dec = {}
for tag, data in (("linux", be_table), ("dual", dual_table), ("own", own_table)):
    bp = os.path.join(OUT, "table-%s.bin" % tag)
    open(bp, "wb").write(data)
    rc, o = run([sys.executable, GEN, "--flash-size", "16MB", bp, os.path.join(OUT, "decoded-%s.csv" % tag)])
    ok(rc == 0, "gen_esp32part.py decodes the %s table (exit %d)" % (tag, rc))
    dec[tag] = [l for l in open(os.path.join(OUT, "decoded-%s.csv" % tag)).read().splitlines() if l and not l.startswith("#")]
p("  decoded Linux table:")
for l in dec["linux"]:
    p("    " + l)
p("  diff, decoded Linux table to decoded dual-boot table:")
for l in difflib.unified_diff(dec["linux"], dec["dual"], "linux", "dual", lineterm="", n=0):
    p("    " + l)
ok(dec["dual"][:len(dec["linux"])] == dec["linux"] and dec["dual"][len(dec["linux"]):] == ["tiny386,app,test,0xe20000,1920K,"],
   "the dual-boot table is the Linux table plus one line, tiny386,app,test,0xe20000,1920K")
ok(dual_table[:7 * 32] == be_table[:7 * 32], "the first 224 bytes (seven Linux entries) are the same bytes")
mg, ty, st, off, size = struct.unpack("<HBBII", dual_table[7 * 32:7 * 32 + 12])
ok((mg, ty, st, off, size) == (0x50AA, 0, 0x20, TEST_OFF, TEST_SIZE) and dual_table[7 * 32 + 12:7 * 32 + 28].rstrip(b"\0") == b"tiny386",
   "entry 8: magic 0x50AA, type app (0), subtype test (0x20), 0x%X, 0x%X, name tiny386" % (off, size))
ok(dual_table[8 * 32:8 * 32 + 2] == b"\xeb\xeb" and
   hashlib.md5(dual_table[:8 * 32]).digest() == dual_table[8 * 32 + 16:8 * 32 + 32],
   "entry 9 is the MD5 entry, and the MD5 over the eight entries is right")
ok(off % 0x10000 == 0 and off + size == FLASH and off >= 0xC20000,
   "0xE20000 is 64 KiB aligned, ends at 16 MiB, starts above the last Linux partition (0xC20000)")
csv = os.path.join(V3, "dualboot/partitions-korvo1-dualboot.csv")
rc, o = run([sys.executable, GEN, "--flash-size", "16MB", csv, os.path.join(OUT, "from-csv.bin")])
ok(rc == 0 and rd(os.path.join(OUT, "from-csv.bin")) == dual_table, "gen_esp32part.py on the CSV gives the table byte for byte")
own_fac = [l for l in dec["own"] if l.startswith("factory")]
p("  tiny386's own table (standalone): " + " | ".join(dec["own"]))

# ------------------------------------------------------------- bootloader
p()
p("== F. The dual-boot bootloader")
bl = rd(os.path.join(V3, "images/korvo1-dualboot/bootloader.bin"))
be_bl = rd(os.path.join(BE_PKG, "bootloader.bin"))
rel_bl = zipfile.ZipFile(REL_ZIP).read("bootloader/bootloader/bootloader.bin")
lim = PT_OFF - BL_OFF
for tag, b in (("dual boot", bl), ("be5b725 (in Plat's flash)", be_bl), ("release v2026.09.20", rel_bl),
               ("tiny386 own (standalone)", rd(os.path.join(V3, "images/korvo1-standalone/bootloader.bin")))):
    p("  %-28s %6d bytes, %5d spare below 0x8000 (%.1f per cent)" % (tag, len(b), lim - len(b), 100.0 * (lim - len(b)) / lim))
ok(len(bl) <= lim, "dual-boot bootloader %d <= %d bytes (0x2000 to 0x8000): %d bytes spare" % (len(bl), lim, lim - len(bl)))
log = open(os.path.join(W, "logs/dual.log"), errors="replace").read()
m = re.search(r"Bootloader binary size 0x([0-9a-f]+) bytes\. 0x([0-9a-f]+) bytes \((\d+)%\) free\.", log)
ok(m is not None and int(m.group(1), 16) == len(bl), "ESP-IDF's own check in the build log: %s" % (m.group(0) if m else "not found"))
ok(bl[2:4] == be_bl[2:4], "image header flash mode and size/speed bytes = be5b725's (0x%02X 0x%02X: DIO, 16MB, 80m)" % (bl[2], bl[3]))
for s_ in (b"Detect a boot condition of the test firmware", b"Test firmware is not found in partition table"):
    ok(s_ in bl and s_ not in be_bl, "string '%s' in the dual-boot bootloader, not in be5b725's" % s_.decode())
hdr = os.path.join(W, "dual/build/bootloader/bootloader/config/sdkconfig.h")
h = open(hdr).read()
want = {"CONFIG_BOOTLOADER_APP_TEST": "1", "CONFIG_BOOTLOADER_NUM_PIN_APP_TEST": "42",
        "CONFIG_BOOTLOADER_APP_TEST_PIN_LOW": "1", "CONFIG_BOOTLOADER_HOLD_TIME_GPIO": "1"}
for k, v in want.items():
    mm = re.search(r"^#define %s (\S+)$" % k, h, re.M)
    ok(mm is not None and mm.group(1) == v, "second-stage bootloader's sdkconfig.h: #define %s %s" % (k, mm.group(1) if mm else "missing"))
ok(re.search(r"^#define CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK\b", h, re.M) is None, "anti-rollback off (APP_TEST requires it)")
a = open(os.path.join(W, "src/linux-be5b725/bootloader/sdkconfig")).read().splitlines()
b = open(os.path.join(W, "dual/bootloader/sdkconfig")).read().splitlines()
d = [l for l in difflib.unified_diff(a, b, "linux sdkconfig", "dual-boot sdkconfig", lineterm="", n=0)]
p("  sdkconfig, Linux be5b725 copy to dual boot:")
for l in d:
    p("    " + l)
# setting lines only ("CONFIG_X=..." or "# CONFIG_X is not set"); the
# "# default:" comment lines menuconfig writes move with them and are not settings
changed = sorted({re.match(r"^(?:# )?(CONFIG_\w+)", l[1:]).group(1) for l in d
                  if l[:1] in "+-" and not l.startswith(("+++", "---")) and re.match(r"^(?:# )?CONFIG_\w+", l[1:])})
ok(set(changed) <= {"CONFIG_BOOTLOADER_APP_TEST", "CONFIG_BOOTLOADER_NUM_PIN_APP_TEST", "CONFIG_BOOTLOADER_APP_TEST_PIN_LOW",
                    "CONFIG_BOOTLOADER_APP_TEST_PIN_HIGH", "CONFIG_BOOTLOADER_HOLD_TIME_GPIO",
                    "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME", "CONFIG_PARTITION_TABLE_FILENAME"},
   "no setting changed but the test pin ones and the table file name: %s" % ", ".join(changed))
live = os.path.expanduser("~/esp32-s31-linux-main/bootloader/sdkconfig")
if os.path.exists(live):
    ok(rd(live) == rd(os.path.join(W, "src/linux-be5b725/bootloader/sdkconfig")),
       "~/esp32-s31-linux-main/bootloader/sdkconfig (read now) = the copy the build started from")
for k in ("CONFIG_BOOTLOADER_LOG_LEVEL", "CONFIG_ESPTOOLPY_FLASHMODE", "CONFIG_ESPTOOLPY_FLASHFREQ", "CONFIG_ESPTOOLPY_FLASHSIZE",
          "CONFIG_PARTITION_TABLE_OFFSET", "CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE", "CONFIG_SPIRAM_BOOT_INIT",
          "CONFIG_BOOTLOADER_COMPILE_TIME_DATE", "CONFIG_SECURE_BOOT", "CONFIG_SECURE_FLASH_ENC_ENABLED"):
    la = [l for l in a if l.startswith(k + "=") or l.startswith(k + "_") and "=" in l and l.split("=")[1] == "y" or l == "# %s is not set" % k]
    lb = [l for l in b if l.startswith(k + "=") or l.startswith(k + "_") and "=" in l and l.split("=")[1] == "y" or l == "# %s is not set" % k]
    p("    %-40s %s" % (k, "; ".join(lb) if la == lb else "DIFFERENT: %s / %s" % (la, lb)))

# ------------------------------------------------------------- image-info
p()
p("== G. esptool image-info on every image (esptool -c esp32s31 image-info FILE, as the scripts run it)")
imgs = [("dual-boot bootloader", os.path.join(V3, "images/korvo1-dualboot/bootloader.bin")),
        ("v3.1 tiny386.bin, dual boot", os.path.join(V3, "images/korvo1-dualboot/tiny386.bin")),
        ("v2 tiny386.bin", os.path.join(V2IMG, "tiny386.bin")),
        ("standalone bootloader", os.path.join(V3, "images/korvo1-standalone/bootloader.bin")),
        ("v3.1 tiny386.bin, standalone", os.path.join(V3, "images/korvo1-standalone/tiny386.bin")),
        ("be5b725 bootloader", os.path.join(BE_PKG, "bootloader.bin")),
        ("be5b725 Linux loader", os.path.join(BE_PKG, "s31-linux-loader.bin")),
        ("release bootloader", os.path.join(V3, "dualboot/linux-v2026.09.20/bootloader/bootloader/bootloader.bin")),
        ("release Linux loader", os.path.join(V3, "dualboot/linux-v2026.09.20/bootloader/s31-linux-loader.bin"))]
os.makedirs(os.path.join(OUT, "image-info"), exist_ok=True)
segs = {}
for tag, path in imgs:
    rc, o = run(["esptool", "-c", "esp32s31", "image-info", path])
    open(os.path.join(OUT, "image-info", re.sub(r"\W+", "-", tag).strip("-") + ".txt"), "w").write(o)
    g = lambda k: (re.search(r"^%s:\s*(.*)$" % re.escape(k), o, re.M) or [None, "?"])[1].strip()
    good = rc == 0 and "(invalid)" not in o and "Chip ID: 32 (ESP32-S31)" in o
    ok(good, "%-30s %8s bytes; %s %s %s; min rev %s; checksum %s; hash %s; %s %s" % (
        tag, g("Image size").split()[0], g("Flash mode"), g("Flash freq"), g("Flash size"),
        g("Minimal chip revision").split(",")[0], g("Checksum").split()[-1], g("Validation hash").split()[-1],
        g("Project name") if "Project name" in o else "", g("App version") if "App version" in o else ""))
    segs[tag] = [(int(la, 16), int(ln, 16)) for ln, la in re.findall(r"^\s+\d+\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+0x[0-9a-f]+\s+.*$", o, re.M)]

p()
p("== H. tiny386 in its partition, and its RAM against the bootloader's")
for tag, path, lim_, part in (("v2 tiny386.bin", os.path.join(V2IMG, "tiny386.bin"), TEST_SIZE, "test 0xE20000"),
                              ("v3.1 tiny386.bin (dual boot)", os.path.join(V3, "images/korvo1-dualboot/tiny386.bin"), TEST_SIZE, "test 0xE20000"),
                              ("v3.1 tiny386.bin (standalone)", os.path.join(V3, "images/korvo1-standalone/tiny386.bin"), int(env_sa["SA_APP_SIZE"]), "factory 0x10000")):
    n = os.path.getsize(path)
    ok(n <= lim_, "%-30s %9d of %9d bytes (%s): %d spare, %.1f per cent" % (tag, n, lim_, part, lim_ - n, 100.0 * (lim_ - n) / lim_))
ok(own_fac and own_fac[0].split(",")[4] == "1792K" and int(env_sa["SA_APP_SIZE"]) == 1792 * 1024,
   "SA_APP_SIZE 1835008 = the factory partition of tiny386's own table (1792K)")
bl_low = min(a for a, n in segs["dual-boot bootloader"])
for tag in ("v2 tiny386.bin", "v3.1 tiny386.bin, dual boot"):
    ram = [(a, n) for a, n in segs[tag] if 0x2F000000 <= a < 0x2F100000]
    hi = max(a + n for a, n in ram) - 1
    ok(hi < bl_low, "%s: highest RAM byte loaded 0x%08X, below the dual-boot bootloader's lowest segment 0x%08X" % (tag, hi, bl_low))

# ------------------------------------------------------------- Linux reach
p()
p("== I. Linux's own flash lists never reach 0xE20000")
z = zipfile.ZipFile(REL_ZIP)
fb = z.read("flash.bat").decode("latin-1")
hi = 0
for a, f in re.findall(r"(0x[0-9a-fA-F]+)\s+(\S+)\s*\^?\s*$", fb, re.M):
    n = z.getinfo(f.replace("\\", "/")).file_size
    a = int(a, 16)
    end = -(-(a + n) // 0x1000) * 0x1000 - 1
    hi = max(hi, end)
    p("    release flash.bat  0x%07x %-48s %8d bytes, erased to 0x%07x" % (a, f, n, end))
ok(hi < TEST_OFF, "release flash.bat: highest byte erased or written 0x%X, below 0xE20000" % hi)
for f in ("Image", "linux.size", "initramfs.cpio", "opensbi.bin", "bootloader/s31-linux-loader.bin",
          "bootloader/bootloader/bootloader.bin", "bootloader/partition_table/partition-table.bin", "flash.bat"):
    ok(sha(z.read(f)) == sha(os.path.join(V3, "dualboot/linux-v2026.09.20", f)),
       "v3\\dualboot\\linux-v2026.09.20\\%s = the zip's" % f.replace("/", "\\"))
fa = open(os.path.join(BE_PKG, "flash_args")).read()
hi = 0
for a, f in re.findall(r"(0x[0-9a-fA-F]+)\s+(\S+)", fa):
    a = int(a, 16)
    end = -(-(a + os.path.getsize(os.path.join(BE_PKG, f))) // 0x1000) * 0x1000 - 1
    hi = max(hi, end)
ok(hi < TEST_OFF, "be5b725 flash_args: highest byte 0x%X, below 0xE20000" % hi)
ok(len(flashbin) - 1 < TEST_OFF, "be5b725 flash.bin at 0x0: %d bytes, last byte 0x%X, below 0xE20000" % (len(flashbin), len(flashbin) - 1))
for k in ("linux-release-small", "linux-release-full", "linux-be5b725-small", "linux-be5b725-full"):
    hi = max(-(-(a + os.path.getsize(f)) // 0x1000) * 0x1000 - 1 for a, f in sets[k])
    ok(hi < TEST_OFF, "back-to-linux.bat %-22s highest byte 0x%07X, below 0xE20000" % (k[6:], hi))

# ------------------------------------------------------------- esptool syntax
p()
p("== J. esptool's own help for every command and option the scripts use (no port opened)")
os.makedirs(os.path.join(OUT, "esptool-help"), exist_ok=True)
helps = {}
for name, args in (("main", ["-h"]), ("write-flash", ["write-flash", "-h"]), ("read-flash", ["read-flash", "-h"]),
                   ("read-mac", ["read-mac", "-h"]), ("image-info", ["image-info", "-h"]), ("version", ["version"])):
    rc, o = run(["esptool"] + args, env=dict(os.environ, COLUMNS="200"))
    helps[name] = o
    open(os.path.join(OUT, "esptool-help", name + ".txt"), "w").write(o)
mh = helps["main"]
ok(re.search(r"--chip\s+-c\b", mh) and "esp32s31" in mh, "main: --chip/-c lists esp32s31")
ok(re.search(r"--port\s+-p\b", mh) and re.search(r"--baud\s+-b\b", mh), "main: --port/-p and --baud/-b")
ok(re.search(r"--after\s+-a\b", mh) and "hard-reset" in mh, "main: --after/-a with hard-reset")
for c in ("write-flash", "read-flash", "read-mac", "image-info", "version", "merge-bin"):
    ok(re.search(r"^\W*%s\s" % c, mh, re.M) is not None, "main: command %s" % c)
wh = helps["write-flash"]
ok(re.search(r"--flash-mode\s+-fm\b", wh) and re.search(r"--flash-freq\s+-ff\b", wh) and re.search(r"--flash-size\s+-fs\b", wh)
   and wh.count("keep") >= 3, "write-flash: -fm, -ff, -fs, each taking keep")
ok(re.search(r"ADDRESS\s+SIZE\s+OUTPUT", helps["read-flash"]) is not None, "read-flash: ADDRESS SIZE OUTPUT")
ok(re.search(r"^\d+\.\d+\.\d+\s*$", helps["version"], re.M) is not None,
   "version prints a line that starts with a digit (what the scripts' findstr takes): %s" % helps["version"].strip().splitlines()[-1])
E = os.path.dirname(subprocess.run([sys.executable, "-c", "import esptool,sys;print(esptool.__file__)"], capture_output=True, text=True).stdout.strip())
lo = open(os.path.join(E, "loader.py")).read()
ok(re.search(r"def read_flash_slow\(.*\n\s+raise NotImplementedInROMError", lo) is not None and
   re.search(r"def read_flash\(.*\n\s+if not self\.IS_STUB:\n\s+return self\.read_flash_slow", lo) is not None,
   "esptool loader.py: read-flash needs the flasher stub on this chip (the ROM-only path raises NotImplementedInROMError)")
t = open(os.path.join(E, "targets/esp32s31.py")).read()
ok("def read_flash_slow" not in t, "targets/esp32s31.py does not supply a ROM read_flash_slow")
ok(os.path.exists(os.path.join(E, "targets/stub_flasher/2/esp32s31.json")), "esptool %s ships an esp32s31 flasher stub (stub_flasher/2/esp32s31.json)" %
   re.search(r'__version__ = "([^"]+)"', open(os.path.join(E, "__init__.py")).read()).group(1))

# ------------------------------------------------------------- merge-bin parse check
p()
p("== K. The exact global and write options, parsed by esptool offline (merge-bin takes the same flash options; no port)")
mb = os.path.join(OUT, "merged.bin")
args = ["esptool", "-c", "esp32s31", "-b", "921600", "-a", "hard-reset", "merge-bin", "-o", mb,
        "-fm", "keep", "-ff", "keep", "-fs", "keep"]
for a, f in sets["dualboot-run2"] + sets["dualboot-run1"]:
    args += ["0x%x" % a, f]
rc, o = run(args)
ok(rc == 0, "esptool %s ... (exit %d)" % (" ".join(args[1:15]), rc))
if rc == 0:
    m_ = rd(mb)
    ok(m_[BL_OFF:BL_OFF + len(bl)] == bl, "bootloader bytes unchanged in the merged image (keep leaves the header alone)")
    t3 = rd(sets["dualboot-run1"][0][1])
    ok(m_[TEST_OFF:TEST_OFF + len(t3)] == t3, "tiny386 bytes at 0xE20000 unchanged")
    os.remove(mb)

p()
p("== Result: %d failed" % len(fails))
for f in fails:
    p("  FAIL " + f)
print("\n".join(rep))
sys.exit(1 if fails else 0)
