# bat-harness-v4.4.txt from the harness run (v4.3's hcol43.py with each version moved on by one): a
# header, a per-case comparison with the v4.3 record (checks\bat-harness-v4.3.txt, all 103 cases),
# then the run's output with the harness folder shown as <harness folder>.
#   python3 hcol43.py RUNFLOW-OUT OLD-V4.2-RECORD OUT HARNESS-WINDOWS-PATH
import re, sys
run, old, out, hdir = sys.argv[1:5]


def cases_of(text):
    parts = re.split(r"^(######## \d+ .*)$", text, flags=re.M)
    res = {}
    for i in range(1, len(parts), 2):
        n = int(parts[i].split()[1])
        body = parts[i + 1]
        calls, writes, exitc, stub_writes = [], [], None, 0
        lines = body.split("\n")
        in_calls = False
        for line in lines:
            if line.startswith("-- the stub's calls:"):
                in_calls = True
                continue
            if in_calls:
                if line.startswith("esptool "):
                    calls.append(re.sub(r'"[^"]*\\(tmp|bk-[a-z0-9]+)\\', r'"<\1>\\', line))
                    if re.search(r"write-flash|erase-flash|erase-region", line):
                        stub_writes += 1
                    continue
                in_calls = False
            m = re.search(r"WOULD WRITE: \S+ (.*)$", line)
            if m:
                writes.append(m.group(1))
            m = re.match(r"^exit (\d+)", line)
            if m:
                exitc = int(m.group(1))
        res[n] = dict(title=parts[i], calls=calls, writes=writes, exit=exitc, stub=stub_writes, body=body)
    return res


t = open(run, encoding="ascii", errors="replace").read().replace("\r\n", "\n").replace(hdir, "<harness folder>")
o = open(old, encoding="ascii", errors="replace").read().replace("\r\n", "\n")
o = o[o.index("==== the run's output"):]
new, prev = cases_of(t), cases_of(o)
stub_total = sum(c["stub"] for c in new.values())
lines = []
same = []
for n in sorted(prev):
    if n not in new:
        lines.append(f"  case {n}: missing in this run")
        continue
    a, b = prev[n], new[n]
    norm = lambda cs: [re.sub(r"<harness folder>|C:\\[^\"]*?\\harness", "<H>", c) for c in cs]
    ca, cb = norm(a["calls"]), norm(b["calls"])
    wa = [re.sub(r"<harness folder>|C:\\[^\"]*?\\harness", "<H>", w) for w in a["writes"]]
    wb = [re.sub(r"<harness folder>|C:\\[^\"]*?\\harness", "<H>", w) for w in b["writes"]]
    d = []
    if a["exit"] != b["exit"]:
        d.append(f"exit {a['exit']} -> {b['exit']}")
    if wa != wb:
        d.append(f"WOULD WRITE {len(wa)} -> {len(wb)} lines" + ("" if len(wa) != len(wb) else " (other files)"))
    if ca != cb:
        added = [c for c in cb if c not in ca]
        gone = [c for c in ca if c not in cb]
        rf = lambda c: re.sub(r'\s+"[^"]*"$', "", c.replace("esptool -c esp32s31 -p COM10 -b 921600 -a no-reset ", "").replace("esptool -c esp32s31 -p COM7 -b 921600 -a no-reset ", ""))
        d.append("esptool calls: " + "; ".join(["+ " + rf(c) for c in added] + ["- " + rf(c) for c in gone]))
    if d:
        lines.append(f"  case {n}: " + " | ".join(d))
    else:
        same.append(n)
appv = re.findall(r"App version: (\S+)", t)
hdr = rf"""The three scripts and flash-paths.cmd as v3.9 left them (v4.0 to v4.4 changed none of them), run
on this PC (cmd.exe) with a stub esptool that never touches a port, 2026-10-24, against the v4.4
images in images\: the flow files of v4.1's to v4.3's runs (made by
checks\bat-harness-make-flow-v3.9.py; the scripts are the same), image-info printing esptool's real
output for the v4.4 Korvo-1 image (build.sh's own image-info file of rel1).  The run had PATH set to
C:\Windows\System32 and C:\Windows only, so no esptool but the stub could be found.  Paths in it are
shown as <harness folder>.
{len(new)} cases: 1 to 69 the boot swap's, 70 to 88 the v3.8 board-safety review's, 89 to {max(new)} v3.9's.
Write calls that reached the stub: {stub_total}.  App version lines printed: {appv.count('v4.4')} "v4.4" (the
image-info of images\korvo1-dualboot\tiny386.bin), and {appv.count('v4.1')} "v4.1" (the stub's invalid image,
ii-invalid.txt, as in v4.3's run).

Against checks\bat-harness-v4.3.txt (all 103 cases, the same scripts with the v4.3 images): every
case whose exit code, WOULD WRITE lines or esptool calls differ, and how.  The cases not listed give
the same exit, the same WOULD WRITE lines and the same esptool calls: {len(same)} of {len(prev)}.
"""
with open(out, "w", encoding="ascii", newline="\n") as f:
    f.write(hdr)
    f.write("\n".join(lines) + "\n\n")
    f.write("Per case in this run: exit code, WOULD WRITE lines, stub writes\n")
    for n in sorted(new):
        c = new[n]
        f.write(f"  {n:3d}  exit {c['exit']}  writes {len(c['writes'])}  stub {c['stub']}  {c['title'][9:].split(' ', 1)[1][:110]}\n")
    f.write("\n==== the run's output\n\n")
    f.write(t[t.index("######## 1 "):].rstrip("\n") + "\n")
print(len(new), "cases;", stub_total, "writes reached the stub;", len(same), "of", len(prev), "old cases the same; App version lines:", sorted(set(appv)), len(appv))
print("\n".join(lines))
