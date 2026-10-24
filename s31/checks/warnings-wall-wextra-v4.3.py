#!/usr/bin/env python3
# -Wall -Wextra on the files v4.3 changed that the boards compile, with each
# board's own compile command (compile_commands.json of the in-tree builds,
# ~/v43/merge/tree/esp/build-k and build-d), beside v4.2's same file compiled
# the same way (written next to it for the run, then removed).  perfport.c is
# new in v4.3 and has no v4.2 file to set beside it.  Counts the warnings of
# each, and names any kind of warning v4.3's file has that v4.2's has not.
import json, os, re, shlex, subprocess, sys
T = os.path.expanduser("~/v43/merge/tree")
FILES = ["vga.c", "pc.c", "i8042.c", "perfport.c", "esp/main/speed.c",
         "esp/main/disk_image.c", "esp/main/status_led.c"]
ENV = ("env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH=$HOME/esp/esp-idf "
       "IDF_TOOLS_PATH=$HOME/.espressif-upstream bash -c '. $HOME/esp/esp-idf/export.sh "
       ">/dev/null 2>&1 && cd %s && %s'")
out = []
for build in ("build-k", "build-d"):
    cc = json.load(open(os.path.join(T, "esp", build, "compile_commands.json")))
    for f in FILES:
        src = os.path.join(T, f)
        ent = [e for e in cc if os.path.realpath(e["file"]) == os.path.realpath(src)]
        if not ent:
            out.append("%s %s: no compile command" % (build, f))
            continue
        e = ent[0]
        args = shlex.split(e["command"])
        i = args.index("-o")
        args[i + 1] = "/dev/null"
        args += ["-Wall", "-Wextra", "-Wno-error"]
        old = subprocess.run(["git", "-C", T, "show", "v4.2:" + f],
                             capture_output=True, text=True)
        runs = [("v4.3", src)]
        base_src = None
        if old.returncode == 0:
            base_src = src.replace(".c", ".v42-warn.c")
            open(base_src, "w").write(old.stdout)
            runs.append(("v4.2", base_src))
        res = {}
        for tag, s in runs:
            a = list(args)
            j = a.index("-c")
            a[j + 1] = s
            cmd = ENV % (e["directory"], " ".join(shlex.quote(x) for x in a))
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
            ws = [l for l in r.stderr.splitlines() if "warning:" in l]
            res[tag] = (r.returncode, ws)
        if base_src:
            os.remove(base_src)
        n3 = len(res["v4.3"][1])
        if "v4.2" in res:
            out.append("%s %-22s v4.3: exit %d, %d warnings; v4.2: exit %d, %d warnings" %
                       (build, f, res["v4.3"][0], n3, res["v4.2"][0], len(res["v4.2"][1])))
            kinds = lambda ws: sorted(set(re.sub(r"^.*?warning: ", "", re.sub(r"\d+", "#", w)) for w in ws))
            for k in [k for k in kinds(res["v4.3"][1]) if k not in kinds(res["v4.2"][1])]:
                out.append("    new kind: " + k)
        else:
            out.append("%s %-22s v4.3: exit %d, %d warnings (new in v4.3)" % (build, f, res["v4.3"][0], n3))
            for w in res["v4.3"][1]:
                out.append("    " + w)
print("\n".join(out))
