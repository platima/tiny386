#!/usr/bin/env python3
# -Wall -Wextra on the files v4.2 changed, with each board's own compile
# command (compile_commands.json of the in-tree builds), beside v4.1's
# same file compiled the same way (written next to it for the run, then
# removed).  Counts the warnings of each.
import json, os, re, shlex, subprocess, sys
T = os.path.expanduser("~/flicker/fix/tree")
FILES = ["vga.c", "esp/main/kbd_input.c", "esp/main/speed.c", "esp/main/esp_main.c"]
ENV = "env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH=$HOME/esp/esp-idf IDF_TOOLS_PATH=$HOME/.espressif-upstream bash -c '. $HOME/esp/esp-idf/export.sh >/dev/null 2>&1 && cd %s && %s'"
out = []
for build in ("build-k", "build-d"):
    cc = json.load(open(os.path.join(T, "esp", build, "compile_commands.json")))
    for f in FILES:
        src = os.path.join(T, f)
        ent = [e for e in cc if os.path.realpath(e["file"]) == os.path.realpath(src)]
        if not ent:
            out.append("%s %s: no compile command" % (build, f)); continue
        e = ent[0]
        args = shlex.split(e["command"])
        i = args.index("-o"); args[i + 1] = "/dev/null"
        args += ["-Wall", "-Wextra", "-Wno-error"]
        res = {}
        base_src = src.replace(".c", ".v41-warn.c")
        open(base_src, "w").write(subprocess.run(["git", "-C", T, "show", "v4.1:" + f],
                                                 capture_output=True, text=True).stdout)
        for tag, s in (("v4.2", src), ("v4.1", base_src)):
            a = [x if os.path.realpath(x) != os.path.realpath(src) or x.startswith("-") else s for x in args]
            # the source is the argument after -c
            j = a.index("-c"); a[j + 1] = s
            cmd = ENV % (e["directory"], " ".join(shlex.quote(x) for x in a))
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
            ws = [l for l in r.stderr.splitlines() if "warning:" in l]
            res[tag] = (r.returncode, ws)
        os.remove(base_src)
        n4, nb = len(res["v4.2"][1]), len(res["v4.1"][1])
        out.append("%s %-22s v4.2: exit %d, %d warnings; v4.1: exit %d, %d warnings" %
                   (build, f, res["v4.2"][0], n4, res["v4.1"][0], nb))
        kinds = lambda ws: sorted(set(re.sub(r"^.*?warning: ", "", re.sub(r"\d+", "#", w)) for w in ws))
        new = [k for k in kinds(res["v4.2"][1]) if k not in kinds(res["v4.1"][1])]
        for k in new:
            out.append("    new kind: " + k)
print("\n".join(out))
