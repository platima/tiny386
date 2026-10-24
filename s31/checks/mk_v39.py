# Makes bat-harness-make-flow-v3.9.py from checks\bootswap\scripts\make_flow_bootswap.py
# (unchanged since the boot swap): the same 69 cases first, then the 19 of the v3.8
# board-safety review (its make_flow_review2.py: cases 70 to 88, with its two cut
# states), then v3.9's own.  Every state also answers a read of 0x2000 for 256 bytes,
# which v3.9's scripts make over Linux's table (the first page of the bootloader).
#
#   python3 mk_v39.py make_flow_bootswap.py OUT.py
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()


def rep(old, new):
    global t
    assert t.count(old) == 1, old[:70]
    t = t.replace(old, new)


rep("# Flow harnesses for the v3 .bat files with tiny386 as a possible default at\n",
    "# v3.9: made from make_flow_bootswap.py by mk_v39.py (beside this file in checks\\).\n"
    "# Cases 1 to 69 are the boot swap's, unchanged; 70 to 88 the v3.8 board-safety\n"
    "# review's (make_flow_review2.py); 89 on v3.9's: writes cut short over Linux's\n"
    "# table, app mode behind a bootloader with no key, the DOS board's image.\n"
    "#\n"
    "# Flow harnesses for the v3 .bat files with tiny386 as a possible default at\n")

# the states: the review's two cut states and two more
rep('    "dual-be5b725bl": (bl_be, pt_dual),\n',
    '    "dual-be5b725bl": (bl_be, pt_dual),\n'
    '    # the v3.8 review: run 2 of "all" cut over Linux\'s table: the dual-boot bootloader\n'
    '    # whole and Linux\'s table (stopped between the two files), or half of it written\n'
    '    "linux-dualbl": (bl_dual, pt_linux),\n'
    '    "linux-cutbl": (bl_dual[:11000] + bytes([255]) * (len(bl_dual) - 11000), pt_linux),\n'
    '    # v3.9: cut right after the first sector was erased, and flash-standalone.bat\'s\n'
    '    # one run cut inside its bootloader\n'
    '    "linux-erasedbl": (bytes([255]) * 0x6000, pt_linux),\n'
    '    "linux-owncut": (bl_own[:8000] + bytes([255]) * (len(bl_own) - 8000), pt_linux),\n')

# the first page of 0x2000 in every state
rep('    put("0x2000", "22000", region[:22000])\n',
    '    put("0x2000", "256", region[:256])\n'
    '    put("0x2000", "22000", region[:22000])\n')

# a backup folder that an older script took of a cut state (v3.8's case 87):
# its slice is linux-cutbl's, with its SHA-256 and this board's MAC line
rep('open(os.path.join(OUT, "rf-full.bin"), "wb").write(pad(fb, 16 << 20))\n',
    'open(os.path.join(OUT, "rf-full.bin"), "wb").write(pad(fb, 16 << 20))\n'
    'bkc = os.path.join(OUT, "bk-cutgen")\n'
    'os.makedirs(bkc, exist_ok=True)\n'
    'cut = pad(states["linux-cutbl"][0], 0x6000) + pad(states["linux-cutbl"][1], 0x1000)\n'
    'open(os.path.join(bkc, "korvo1-2000-8fff-before.bin"), "wb").write(cut)\n'
    'open(os.path.join(bkc, "korvo1-2000-8fff-before.bin.sha256"), "wb").write(\n'
    '    (hashlib.sha256(cut).hexdigest() + CRLF).encode("ascii"))\n'
    'open(os.path.join(bkc, "korvo1-before-mac.txt"), "wb").write(\n'
    '    ("MAC:                7c:2c:67:00:00:01" + CRLF).encode("ascii"))\n'
    'open(os.path.join(bkc, "korvo1-before.txt"), "wb").write(\n'
    '    ("made by the harness: what a script older than v3.9 took after a cut write" + CRLF).encode("ascii"))\n')

DOS = 'V3_WIN + BS + "images" + BS + "dosboard" + BS + "tiny386.bin"'
extra = '''  # 70 to 88: the v3.8 board-safety review's own cases (make_flow_review2.py), unchanged
  ("REVIEW dualboot COM10 tiny386 (no mode word), Linux be5b725, no backup yet, Y: all with tiny386 the default", "Y", D, "COM10 tiny386", "linux-be5b725", {"BACKUP_DIR": "bk-r1"}, ['if exist "%~dp0bk-r1" rmdir /s /q "%~dp0bk-r1"'], SHOWBK),
  ("REVIEW dualboot COM10 app, the tiny386-default table behind be5b725's bootloader (a cut write), Y", "Y", D, "COM10 app", "dual386-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 (all), the tiny386-default table behind be5b725's bootloader, Y: the set again", "Y", D, "COM10", "dual386-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 all linux, the Linux-default table behind be5b725's bootloader, Y", "Y", D, "COM10 all linux", "dual-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 table tiny386 app (conflicting words): refused", "Y", D, "COM10 table tiny386 app", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 all tiny386, the standalone set in flash: stops", "Y", D, "COM10 all tiny386", "own", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 all tiny386, a table nobody knows: stops", "Y", D, "COM10 all tiny386", "other", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 table linux, Linux's own table: stops", "Y", D, "COM10 table linux", "linux-be5b725", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW back-to-linux COM10, the Linux-default table behind be5b725's bootloader, no backup, Y", "Y", B, "COM10", "dual-be5b725bl", {"BACKUP_DIR": "bk-none"}, [], []),
  ("REVIEW back-to-linux COM10 backup, the tiny386-default table behind be5b725's bootloader, Y", "Y", B, "COM10 backup", "dual386-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW standalone COM10, the tiny386-default table behind be5b725's bootloader, answer N", "N", S, "COM10", "dual386-be5b725bl", {"BACKUP_DIR": "bk-e"}, [], []),
  ("REVIEW dualboot COM10 table tiny386, KORVO1_MAC set and matching, the dual boot in flash, Y", "Y", D, "COM10 table tiny386", "dual", {"BACKUP_DIR": "bk-a", "KORVO1_MAC": "7c:2c:67:00:00:01"}, [], []),
  ("REVIEW dualboot COM10 all tiny386, Linux, no backup yet, the 16 MB read fails, Y: nothing written", "Y", D, "COM10 all tiny386", "linux-be5b725", {"BACKUP_DIR": "bk-r2", "FAKE_FAIL": "full"}, ['if exist "%~dp0bk-r2" rmdir /s /q "%~dp0bk-r2"'], SHOWBK),
  ("REVIEW dualboot COM10 app, tiny386 the default, image-info says invalid: stops", "Y", D, "COM10 app", "dual386", {"BACKUP_DIR": "bk-a", "FAKE_II": "invalid"}, [], []),
  ("REVIEW dualboot COM10 again after run 2 was stopped between the bootloader and the table (dual-boot bootloader, Linux's table), the backup there, Y", "Y", D, "COM10", "linux-dualbl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 all tiny386 again after run 2 was cut inside the bootloader (Linux's table), the backup there, Y", "Y", D, "COM10 all tiny386", "linux-cutbl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW back-to-linux COM10 after run 2 was cut inside the bootloader, the backup there, Y", "Y", B, "COM10", "linux-cutbl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("REVIEW dualboot COM10 after the cut, the backup folder renamed as the stop says (no backup), Y: a new backup of the cut state", "Y", D, "COM10", "linux-cutbl", {"BACKUP_DIR": "bk-r3"}, ['if exist "%~dp0bk-r3" rmdir /s /q "%~dp0bk-r3"'], SHOWBK),
  ("REVIEW back-to-linux COM10 later, with that new backup (default: backup), Y: what it writes back", "Y", B, "COM10", "dual", {"BACKUP_DIR": "bk-r3"}, [], []),
  # 89 on: v3.9
  ("V39 dualboot COM10, the first page at 0x2000 erased in front of Linux's table (a cut just after the erase), the backup there, Y: kept, the set written", "Y", D, "COM10", "linux-erasedbl", {"BACKUP_DIR": "bk-a"}, [], SHOWBK),
  ("V39 dualboot COM10, run 2 cut inside the bootloader, the backup an older script took of that cut state: stops, names back-to-linux small", "Y", D, "COM10", "linux-cutbl", {"BACKUP_DIR": "bk-cutgen"}, [], []),
  ("V39 dualboot COM10 app, the Linux-default table behind be5b725's bootloader: stops, names all", "Y", D, "COM10 app", "dual-be5b725bl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("V39 dualboot COM10 app, the dual boot in flash, answer Y: the bootloader read, then tiny386 alone (what Plat runs next)", "Y", D, "COM10 app", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
  ("V39 dualboot COM10 app, TINY386_BIN the DOS board's image: refused before the board is read", "Y", D, "COM10 app", "dual", {"BACKUP_DIR": "bk-a", "TINY386_BIN": ''' + DOS + '''}, [], []),
  ("V39 dualboot COM10 all, TINY386_BIN the DOS board's image: refused", "Y", D, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-a", "TINY386_BIN": ''' + DOS + '''}, [], []),
  ("V39 standalone COM10, SA_TINY386_BIN the DOS board's image: refused", "Y", S, "COM10", "linux-be5b725", {"BACKUP_DIR": "bk-e", "SA_TINY386_BIN": ''' + DOS + '''}, [], []),
  ("V39 standalone COM10, its own one run cut inside its bootloader (Linux's table), its backup there, Y: kept, the set written", "Y", S, "COM10", "linux-owncut", {"BACKUP_DIR": "bk-e"}, [], SHOWBK),
  ("V39 standalone COM10, a dual-boot install's run 2 cut inside the bootloader, that install's backup there, Y: kept, the set written", "Y", S, "COM10", "linux-cutbl", {"BACKUP_DIR": "bk-a"}, [], []),
  ("V39 dualboot COM10, flash-standalone.bat cut inside its bootloader, its backup there, Y: kept, the dual-boot set written", "Y", D, "COM10", "linux-owncut", {"BACKUP_DIR": "bk-e"}, [], []),
  ("V39 standalone COM10, a cut write and no backup: stops, nothing taken", "Y", S, "COM10", "linux-cutbl", {"BACKUP_DIR": "bk-r4"}, ['if exist "%~dp0bk-r4" rmdir /s /q "%~dp0bk-r4"'], SHOWBK),
  ("V39 dualboot COM10, run 2 stopped between the bootloader and the table, no backup: stops, nothing taken", "Y", D, "COM10", "linux-dualbl", {"BACKUP_DIR": "bk-r5"}, ['if exist "%~dp0bk-r5" rmdir /s /q "%~dp0bk-r5"'], SHOWBK),
  ("V39 back-to-linux COM10 small, after a cut over Linux's table with no backup, Y: be5b725's pair", "Y", B, "COM10 small", "linux-cutbl", {"BACKUP_DIR": "bk-r5"}, [], []),
  ("V39 dualboot COM10 all, Linux with the release's bootloader, a backup of be5b725's state: stops, the message names both ways", "Y", D, "COM10", "linux-release", {"BACKUP_DIR": "bk-a"}, [], []),
  ("V39 dualboot COM10 table tiny386, the dual boot in flash, Y (what Plat may run next)", "Y", D, "COM10 table tiny386", "dual", {"BACKUP_DIR": "bk-a"}, [], []),
'''
rep("]\nrun = [", extra + "]\nrun = [")
rep('keys = ["FAKE_FLASH", "FAKE_BOARD", "FAKE_MAC", "FAKE_FAIL", "FAKE_II", "BACKUP_DIR",\n        "TINY386_BIN", "KORVO1_MAC", "SA_TABLE"]',
    'keys = ["FAKE_FLASH", "FAKE_BOARD", "FAKE_MAC", "FAKE_FAIL", "FAKE_II", "BACKUP_DIR",\n        "TINY386_BIN", "KORVO1_MAC", "SA_TABLE", "SA_TINY386_BIN"]')
open(dst, "w").write(t)
print("written", dst)
