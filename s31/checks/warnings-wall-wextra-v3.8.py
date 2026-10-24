# The files v3.8 changed, compiled again with each build's own command line
# (compile_commands.json) plus -Wall -Wextra -Wno-unused-parameter, to
# assembly, thrown away; the warnings that point into the file itself.  Then
# the same file as v3.7 had it (OLDTREE, an export of the v3.7 tag, put in
# place of TREE in the command line, its own headers with it), to tell what
# is new.  python3 warn38.py BUILD_DIR TREE OLDTREE file...
import json, os, re, shlex, subprocess, sys

build, tree, old = sys.argv[1], sys.argv[2], sys.argv[3]
cc = json.load(open(os.path.join(build, "compile_commands.json")))

def run(e, f, root):
    # the sources and headers from root; the build folder (generated
    # headers, the working directory) stays the build's
    cmd = re.sub(re.escape(tree) + r"(?!/esp/build-)", root, e["command"])
    d = e["directory"]
    args, out, skip = shlex.split(cmd), [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a == "-o":
            skip = True
            continue
        out.append(a)
    out += ["-Wall", "-Wextra", "-Wno-unused-parameter", "-S", "-o", "/dev/null"]
    r = subprocess.run(out, cwd=d, capture_output=True, text=True)
    base = os.path.basename(f)
    w = [l.replace(root, "<" + ("v3.7" if root == old else "v3.8") + ">")
         for l in r.stderr.splitlines()
         if ("warning:" in l or "error:" in l) and l.split(":")[0].endswith("/" + base)]
    return r.returncode, w

total_new = total_old = 0
for f in sys.argv[4:]:
    hits = [e for e in cc if e["file"].endswith("/" + f)]
    if not hits:
        print("%-22s not in this build" % f)
        continue
    for root, tag in ((tree, "v3.8"), (old, "v3.7")):
        if not os.path.exists(os.path.join(root, f)):
            print("  %-20s %s: no such file" % (f, tag))
            continue
        rc, w = run(hits[0], f, root)
        if root == tree:
            total_new += len(w)
        else:
            total_old += len(w)
        print("  %-20s %s: %d warnings in the file (exit %d)" % (f, tag, len(w), rc))
        for l in w:
            print("      " + l)
print("total: v3.8 %d, v3.7 %d" % (total_new, total_old))
