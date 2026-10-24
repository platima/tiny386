#!/usr/bin/env python3
# v3.9's new checks against faults put into copies of the files, one at a time: each fault
# must make the test named for it fail, and the unchanged files must pass.  The tree is an
# export of the v3.9 tag (no git, so run_cpu.sh's runs against older tags are left out);
# nothing in the scratch tree is touched.
#   python3 -I mutants-v3.9.py TREE_EXPORT OUT
import os, shutil, subprocess, sys

SRC, OUT = sys.argv[1], sys.argv[2]
CF = "-O1 -g -w".split()

M = [
    ("crash-no-say", "esp/main/console.c", "\tcrash_say_held();\n\tint tail = console_ring_tail", "\tint tail = console_ring_tail", "2"),
    ("count-grey", "esp/main/console.c", "\t\temit(k->level, line,", "\t\temit(CON_INFO, line,", "2"),
    ("level-not-kept", "esp/main/console.c", "\tif (level > k->level)\n\t\tk->level = level;\n", "", "2"),
    ("held-not-cleared", "esp/main/console.c", "\t\tk->held = 0;    /* said */\n", "", "2"),
    ("crash-oldest-not-newest", "esp/main/console.c", "\tk->held++;\n\tif (level > k->level)", "\tk->held++;\n\tif (k->held > 1) return false;\n\tif (level > k->level)", "2"),
    ("shutdown-note-after", "i386.c", "\t\t\tcpu_abort_begin();\n\t\t\tdolog(\"shutdown:", "\t\t\tdolog(\"shutdown:", "20"),
    ("note-twice", "i386.c", "\tcpu_abort_note = NULL;\n\tif (note)", "\tif (note)", "20"),
    ("try1-note-after", "i386.c", "\tcpu_abort_begin();\n\tdolog(\"@ %s %s %d\\n\", file, fn, line);", "\tdolog(\"@ %s %s %d\\n\", file, fn, line);\n\tcpu_abort_begin();", "20"),
    ("try1-old-macro", "i386.c", "#define TRY1(f) if(unlikely(!(f))) { cpu_abort_at(cpu, __FILE__, __FUNCTION__, __LINE__); }",
     "#define TRY1(f) if(unlikely(!(f))) { dolog(\"@ %s %s %d\\n\", __FILE__, __FUNCTION__, __LINE__); cpu_abort(cpu, -1); }", "20"),
    ("stall-uart-only", "esp/main/status_led.c", "console_printf(CON_WARN, \"status LED: the emulator has made no \"", "console_uart_printf(\"status LED: the emulator has made no \"", "15"),
    ("stall-grey", "esp/main/status_led.c", "console_printf(CON_WARN, \"status LED: the emulator has made no \"", "console_printf(CON_INFO, \"status LED: the emulator has made no \"", "15"),
    ("goes-on-uart-only", "esp/main/status_led.c", "console_printf(CON_INFO, \"status LED: the emulator goes on again\")", "console_uart_printf(\"status LED: the emulator goes on again\")", "15"),
]


def run_test(tree, test, out):
    os.makedirs(out, exist_ok=True)
    if test == "2":
        b = os.path.join(out, "tc")
        r = subprocess.run(["gcc"] + CF + ["-DCONSOLE_HOST", "-I" + tree + "/esp/main", "-o", b,
                            tree + "/tests/host/test_console.c", tree + "/esp/main/console.c", tree + "/vgafont.c"],
                           capture_output=True, text=True)
        if r.returncode:
            return "build failed: " + r.stderr[-300:]
        r = subprocess.run([b, out], capture_output=True, text=True)
        fails = [l.strip() for l in r.stdout.splitlines() if "FAIL" in l]
        return "pass" if r.returncode == 0 else "FAIL (%d): %s" % (len(fails), "; ".join(fails)[:300])
    if test == "15":
        b = os.path.join(out, "ts")
        r = subprocess.run(["gcc"] + CF + ["-DSTATUS_HOST", "-I" + tree + "/esp/main", "-o", b,
                            tree + "/tests/host/test_status.c", tree + "/esp/main/status_code.c"],
                           capture_output=True, text=True)
        if r.returncode:
            return "build failed: " + r.stderr[-300:]
        r = subprocess.run([b, tree + "/esp/main"], capture_output=True, text=True)
        fails = [l.strip() for l in r.stdout.splitlines() if "FAIL" in l]
        return "pass" if r.returncode == 0 else "FAIL (%d): %s" % (len(fails), "; ".join(fails)[:300])
    if test == "20":
        r = subprocess.run(["bash", tree + "/tests/host/cpu/run_cpu.sh", out], capture_output=True, text=True)
        fails = [l.strip() for l in r.stdout.splitlines() if "FAILED" in l]
        logf = [os.path.join(out, x) for x in ("korvo1.log", "plain.log")]
        inner = []
        for lf in logf:
            if os.path.exists(lf):
                inner += [l.strip() for l in open(lf) if "FAIL line" in l]
        return "pass" if r.returncode == 0 else "FAIL: %s %s" % ("; ".join(fails)[:200], ("| " + "; ".join(inner)[:200]) if inner else "")
    raise ValueError(test)


os.makedirs(OUT, exist_ok=True)
print("unchanged files:")
for test in ("2", "15", "20"):
    print("  test %s: %s" % (test, run_test(SRC, test, os.path.join(OUT, "base-" + test))))
caught = 0
for name, f, old, new, test in M:
    d = os.path.join(OUT, "tree-" + name)
    if os.path.exists(d):
        shutil.rmtree(d)
    shutil.copytree(SRC, d, symlinks=True)
    p = os.path.join(d, f)
    t = open(p).read()
    assert t.count(old) == 1, (name, t.count(old))
    open(p, "w").write(t.replace(old, new))
    res = run_test(d, test, os.path.join(OUT, "out-" + name))
    ok = res.startswith("FAIL")
    caught += ok
    print("%-26s %-22s test %-3s %s" % (name, f, test, ("caught: " if ok else "NOT CAUGHT: ") + res))
    shutil.rmtree(d)
print("%d of %d faults caught" % (caught, len(M)))
