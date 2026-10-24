import sys, csv
p = sys.argv[1]
rows = list(csv.DictReader(open(p + "/frames.tsv"), delimiter="\t"))
bad = [r for r in rows if r["kind"] in "LC" and int(r["d_panel"]) > 0]
print(len(rows), "steps;", len(bad), "differing")
# group runs of consecutive differing steps
grp = []
for r in bad:
    s = int(r["step"])
    if grp and s - grp[-1][-1][0] <= 2:
        grp[-1].append((s, r))
    else:
        grp.append([(s, r)])
for g in grp[:40]:
    a, b = g[0][1], g[-1][1]
    mx = max(int(x[1]["d_panel"]) for x in g)
    bl = max(int(x[1]["d_black"]) for x in g)
    modes = sorted(set((x[1]["gm_now"], x[1]["gm_drawn"], x[1]["vbe_on"], x[1]["xres"], x[1]["yres"], x[1]["bpp"]) for x in g))
    print("steps %s-%s t=%s-%s n=%d max_px=%d max_black=%d rows %s-%s modes %s" % (a["step"], b["step"], a["t"], b["t"], len(g), mx, bl, a["d_r0"], a["d_r1"], modes))
print(len(grp), "groups")
