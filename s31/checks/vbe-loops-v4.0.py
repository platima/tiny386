#!/usr/bin/env python3
# loops.py DISASSEMBLY: the innermost loops (a backward branch over at most
# 200 bytes, with no other backward branch inside), with their instructions,
# from objdump -d --no-show-raw-insn output
import re, sys
ins = []
for line in open(sys.argv[1]):
    m = re.match(r"^\s*([0-9a-f]+):\t(\S+)\s*(.*)$", line.rstrip("\n"))
    if m:
        ins.append((int(m.group(1), 16), m.group(2), m.group(3)))
idx = {a: i for i, (a, _, _) in enumerate(ins)}
loops = []
for i, (a, op, args) in enumerate(ins):
    if not (op.startswith("b") or op == "j"):
        continue
    m = re.search(r"\b([0-9a-f]{8})\b", args)
    if not m:
        continue
    t = int(m.group(1), 16)
    if t < a and a - t <= 256 and t in idx:
        loops.append((idx[t], i))
# innermost: no other loop strictly inside
inner = [(s, e) for (s, e) in loops if not any(s <= s2 and e2 <= e and (s2, e2) != (s, e) for (s2, e2) in loops)]
for s, e in inner:
    body = ins[s:e + 1]
    print("loop %x..%x: %d instructions" % (ins[s][0], ins[e][0], len(body)))
    for a, op, args in body:
        print("    %x  %-8s %s" % (a, op, args))
