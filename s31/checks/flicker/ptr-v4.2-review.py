import sys, csv, re, collections
# ptr.py RUNDIR T0 T1 (seconds from the session start): the pointer's counts in the box, panel and reference
d, t0, t1 = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
log = open(d + "/log.txt", errors="replace").read()
desk = float(re.search(r"taskbar first seen at t=([0-9.]+)", log).group(1))
s = desk + 10
rows = [r for r in csv.DictReader(open(d + "/frames.tsv"), delimiter="\t") if s + t0 <= float(r["t"]) < s + t1]
c = collections.Counter((int(r["cur_white"]) + int(r["cur_black"]), int(r["ref_white"]) + int(r["ref_black"])) for r in rows)
print(len(rows), "pictures; (panel count, reference count): pictures")
for k, v in sorted(c.items(), key=lambda x: -x[1])[:15]:
    print("  ", k, v)
diff = sum(1 for r in rows if int(r["cur_white"]) != int(r["ref_white"]) or int(r["cur_black"]) != int(r["ref_black"]))
print("pictures where the panel's pointer pixels differ from the reference's:", diff)
# the steps with the commonest small count, for a look
small = [r["step"] for r in rows if 0 < int(r["cur_white"]) + int(r["cur_black"]) < 100][:5]
print("some steps with fewer than 100 pointer pixels:", small)
