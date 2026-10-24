#!/usr/bin/env python3
"""Mutation run for v3.8's new tests: faults put one at a time into a copy
of the tree's files, and the tests that should see them run against each.
Test 15 (test_status), test 2 (test_console), test_floppy and test 14 (the
disk test, through ESP-IDF's FatFs).  Each fault must make at least one of
its tests fail; the unchanged files must pass.

  python3 mutants-v3.8.py TREE WORK
"""
import os, shutil, subprocess, sys

TREE, WORK = sys.argv[1], sys.argv[2]
FILES = ["esp/main/status_code.c", "esp/main/status_code.h", "esp/main/status_led.c",
         "esp/main/esp_main.c", "esp/main/console.c", "esp/main/console.h", "ide.c", "misc.c"]
BS = chr(92)
CF = "-O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"

def sh(cmd):
    return subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True).returncode

def run(test, t):
    o = os.path.join(WORK, "out")
    os.makedirs(o, exist_ok=True)
    h = os.path.join(t, "tests/host")
    if test == "status":
        return sh(f"gcc {CF} -DSTATUS_HOST -I{t}/esp/main -o {o}/ts {h}/test_status.c "
                  f"{t}/esp/main/status_code.c && {o}/ts {t}/esp/main")
    if test == "console":
        return sh(f"gcc {CF} -DCONSOLE_HOST -I{t}/esp/main -o {o}/tc {h}/test_console.c "
                  f"{t}/esp/main/console.c {t}/vgafont.c && {o}/tc {o}")
    if test == "floppy":
        return sh(f"gcc -O1 -g -w -DEMULINK_CHECKED -I{t} -c -o {o}/m.o {t}/misc.c && "
                  f"gcc {CF} -DEMULINK_CHECKED -I{t} -o {o}/tf {h}/test_floppy.c {o}/m.o && {o}/tf {o}")
    if test == "disk":
        r = sh(f"env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH=$HOME/esp/esp-idf "
               f"bash {h}/disk/run_disk.sh {o}/disk > {o}/disk.log 2>&1")
        # a SKIP (no mkfs.fat, no ESP-IDF) is no pass
        return r if "SKIP" not in open(f"{o}/disk.log").read() else 2
    raise SystemExit(test)

# (name, file, old, new, tests that must fail)
M = [
 ("running green ignores the disk light", "esp/main/status_code.c",
  "return s->disk ? DISK_LIGHT : GREEN;", "return GREEN;", ["status"]),
 ("v3.7's dark blink back", "esp/main/status_code.c",
  "return s->disk ? DISK_LIGHT : GREEN;", "return now_ms % 2000 < 100 ? BLACK : s->disk ? DISK_LIGHT : GREEN;", ["status"]),
 ("the disk light green, not blue", "esp/main/status_code.c",
  "#define DISK_LIGHT      BLUE", "#define DISK_LIGHT      CYAN", ["status"]),
 ("the disk light over an error", "esp/main/status_code.c",
  "	int e = status_shown_error(s);\n	if (e)",
  "	if (s->disk && s->stage == STATUS_STAGE_RUNNING)\n		return BLUE;\n	int e = status_shown_error(s);\n	if (e)", ["status"]),
 ("looks every 50 ms", "esp/main/status_code.h",
  "#define STATUS_TICK_MS    20", "#define STATUS_TICK_MS    50", ["status"]),
 ("a look always lit", "esp/main/status_code.c",
  "bool moved = count != d->last;", "bool moved = true;", ["status"]),
 ("the count never kept", "esp/main/status_code.c",
  "	d->last = count;\n	return moved;", "	return moved;", ["status"]),
 ("led_task without the disk step", "esp/main/status_led.c",
  "st.disk = count && running && status_disk_step(&disk, count());", "st.disk = 0;", ["status"]),
 ("bf_write_async not counted", "ide.c",
  "    BF_ACTIVITY();\n    switch(bf->mode) {", "    switch(bf->mode) {", ["status", "disk"]),
 ("bf_read_async counted after the range check", "ide.c",
  "    BF_ACTIVITY();\n#ifdef BUILD_ESP32\n    /* Nothing past",
  "#ifdef BUILD_ESP32\n    /* Nothing past", ["status", "disk"]),
 ("the floppy's read not counted", "misc.c",
  "			FDD_ACTIVITY();\n			int ret = fread(", "			int ret = fread(", ["status", "floppy"]),
 ("the count given after the PC runs", "esp/main/esp_main.c",
  "	status_led_disk_counter(disk_activity);\n	status_led_stage(STATUS_STAGE_RUNNING);",
  "	status_led_stage(STATUS_STAGE_RUNNING);\n	status_led_disk_counter(disk_activity);", ["status"]),
 ("the first 6 of a kind", "esp/main/console.c",
  "#define LIM_FIRST     5", "#define LIM_FIRST     6", ["console"]),
 ("a period of 4 s", "esp/main/console.c",
  "#define LIM_PERIOD_MS 5000", "#define LIM_PERIOD_MS 4000", ["console"]),
 ("numbers kept in a line's kind", "esp/main/console.c",
  "		if (digit)\n			h = (h ^ '#') * 16777619u;\n		else\n", "		(void) digit;\n", ["console"]),
 ("console_printf() held back too", "esp/main/console.c",
  "		if (!limited || lim_pass(p, n))", "		if (lim_pass(p, n))", ["console"]),
 ("the count not reset after it is said", "esp/main/console.c",
  "	k->start_ms = now;\n	k->shown = 0;\n	k->held = 0;\n}", "	k->start_ms = now;\n	k->shown = 0;\n}", ["console"]),
 ("a place taken without its count said", "esp/main/console.c",
  "		k = spare;\n		lim_say(k, now);", "		k = spare;\n		k->held = 0;\n		lim_say(k, now);", ["console"]),
 ("the limit's tick never says", "esp/main/console.c",
  "		if (k->held && (all || now - k->start_ms >= LIM_PERIOD_MS))", "		if (k->held && all)", ["console"]),
 ("the counts not said when the limit goes off", "esp/main/console.c",
  "		if (s_lim_on)\n			lim_say_due(true);", "		(void) 0;", ["console"]),
 ("the queue sends LF, not CR LF", "esp/main/console.c",
  "		if (c == '" + BS + "n') {\n			uart_put('" + BS + "r');\n			room--;\n		}", "", ["console"]),
 ("the queue overfills the FIFO", "esp/main/console.c",
  "		if (room < (c == '" + BS + "n' ? 2u : 1u))\n			break;", "", ["console"]),
 ("dropped lines not counted", "esp/main/console.c",
  "	} else {\n		s_uq_dropped++;\n	}", "	}", ["console"]),
 ("the queue left as it is when turned off", "esp/main/console.c",
  "		while (uq_out()) {", "		while (0) {", ["console"]),
 ("the UART written straight while queued", "esp/main/console.c",
  "	if ((to & TO_UART) && s_uq_on) {", "	if (0) {", ["console"]),
]

src = os.path.join(WORK, "tree")
if os.path.exists(src):
    shutil.rmtree(src)
# a copy of what the tests read
shutil.copytree(TREE, src, ignore=shutil.ignore_patterns("build*", "managed_components", ".git"))
orig = {f: open(os.path.join(src, f)).read() for f in FILES}
fails = 0
print("unchanged files:")
for t in ["status", "console", "floppy", "disk"]:
    r = run(t, src)
    print(f"  {t}: {'pass' if r == 0 else 'FAIL'}")
    fails += r != 0
caught = 0
for name, f, old, new, tests in M:
    if orig[f].count(old) != 1:
        print(f"MUTANT NOT APPLIED ({orig[f].count(old)} matches): {name}")
        fails += 1
        continue
    open(os.path.join(src, f), "w").write(orig[f].replace(old, new))
    res = {t: run(t, src) for t in tests}
    open(os.path.join(src, f), "w").write(orig[f])
    seen = [t for t in tests if res[t] != 0]
    ok = len(seen) == len(tests)
    caught += ok
    print(f"{'caught' if ok else 'MISSED'}: {name} ({f}): failed {', '.join(seen) or 'none'} of {', '.join(tests)}")
    fails += not ok
print(f"mutants: {caught} of {len(M)} caught by every test named; unchanged files pass: {'yes' if fails == len(M) - caught else 'no'}")
sys.exit(1 if fails else 0)
