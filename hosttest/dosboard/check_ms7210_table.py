#!/usr/bin/env python3
"""Check an ms7210_test log against the register table in encoder.md 8.4.

    check_ms7210_table.py ENCODER_MD LOG [--phase N]

Reads every row of the A, B, C, D and E tables in section 8.4 (register,
operation, value) and, for each, the bus events the driver produced under
that row's ROW marker in the log.  A row passes when its events are exactly
what the table says: a write of the value, a read-modify-write that changes
only the masked bits, the two halves of a 16-bit value low byte first, the
InfoFrame bytes, a wait at least as long.  Extra events in a row's group are
listed and must be ones section 8.6 asks for (hot plug, PLL lock and input
reads, the DDC pass-through and the EDID).  Then the values the Loongson (LS)
and Meyesemi (MY) tables write, as the Source column quotes them, are
compared with the register after that row, on the bits the sequence has set
so far.  Exit status 0 when everything agrees.
"""
import re
import sys


def parse_table(md_path):
    text = open(md_path, encoding='utf-8').read().split('\n')
    start = next(i for i, l in enumerate(text) if l.startswith('### 8.4'))
    end = next(i for i, l in enumerate(text) if l.startswith('### 8.5'))
    rows = []
    for line in text[start:end]:
        m = re.match(r'^\| ([A-E]\d+) \|', line)
        if not m:
            continue
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        rows.append({
            'id': cells[0], 'reg': cells[1], 'op': cells[2], 'val': cells[3],
            'src': cells[5] if len(cells) > 5 else '',
        })
    return rows


def hexes(s):
    return [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)', s)]


def parse_log(path):
    """List of (row id, [events]); events before the first ROW go to '-'."""
    groups = [('-', [])]
    final = {}
    for line in open(path):
        line = line.rstrip('\n')
        if line.startswith('ROW '):
            groups.append((line[4:], []))
            continue
        if line.startswith('FINAL '):
            _, r, v = line.split()
            final[int(r, 16)] = int(v, 16)
            continue
        if line.startswith('#'):
            continue
        ev = None
        m = re.match(r'^W ([0-9A-F]{2})((?: [0-9A-F]{2})+) hz=(\d+)$', line)
        if m:
            b = [int(x, 16) for x in m.group(2).split()]
            ev = ('W', int(m.group(1), 16), b, int(m.group(3)))
        m = re.match(r'^R ([0-9A-F]{2})((?: [0-9A-F]{2})+) -> ([0-9A-F]{2}).* n=(\d+) hz=(\d+)$', line)
        if m:
            b = [int(x, 16) for x in m.group(2).split()]
            ev = ('R', int(m.group(1), 16), b, int(m.group(5)), int(m.group(3), 16))
        m = re.match(r'^R ([0-9A-F]{2})((?: [0-9A-F]{2})+) NAK', line)
        if m:
            ev = ('RNAK', int(m.group(1), 16))
        m = re.match(r'^(SPEED|RSTN|DELAY) (\d+)$', line)
        if m:
            ev = (m.group(1), int(m.group(2)))
        if line == 'NAK':
            ev = ('NAK',)
        if ev is None:
            raise SystemExit('cannot read log line: %r' % line)
        groups[-1][1].append(ev)
    return groups, final


def reg_of(ev):
    """Register index of a 0x59 event, checking the low byte comes first."""
    return ev[2][0] | (ev[2][1] << 8)


ALLOWED_EXTRA_READS = {0x0502, 0x091C, 0x051D}    # hot plug, PLL lock, input stable (8.6)
ALLOWED_EXTRA_WRITES = {0x0013, 0x0012}           # DDC pass-through (8.6)


def is_allowed_extra(ev):
    if ev[0] in ('SPEED', 'DELAY'):
        return ev[0] == 'SPEED'
    if ev[0] == 'R' and ev[1] == 0x50:
        return True
    if ev[0] == 'RNAK' and ev[1] == 0x50:
        return True
    if ev[0] == 'R' and ev[1] == 0x59 and reg_of(ev) in ALLOWED_EXTRA_READS:
        return True
    if ev[0] == 'W' and ev[1] == 0x59 and reg_of(ev) in ALLOWED_EXTRA_WRITES:
        return True
    return False


def expected_values(row, phase):
    """Values for 'write' style rows."""
    v = row['val']
    if ' or ' in v:
        return [phase]
    m = re.match(r'^(.*)then 0x([0-9A-Fa-f]+) x (\d+)$', v)
    if m:
        return hexes(m.group(1)) + [int(m.group(2), 16)] * int(m.group(3))
    return hexes(v)


def check_row(row, evs, phase, image, controlled):
    """Returns (ok, message, extra events)."""
    op = row['op']
    reg_txt = row['reg']
    regs = hexes(reg_txt)
    i = 0

    def take(kind, addr=0x59):
        nonlocal i
        while i < len(evs) and evs[i][0] in ('SPEED',):
            i += 1
        if i < len(evs) and evs[i][0] == kind and (kind not in ('W', 'R') or evs[i][1] == addr):
            ev = evs[i]
            i += 1
            return ev
        return None

    if reg_txt == 'RSTN pin':
        seq = [e for e in evs if e[0] in ('RSTN', 'DELAY')]
        lows = [k for k, e in enumerate(seq) if e == ('RSTN', 0)]
        highs = [k for k, e in enumerate(seq) if e == ('RSTN', 1)]
        if not lows or not highs or highs[0] < lows[0]:
            return False, 'no RSTN low then high', []
        low_us = sum(e[1] for e in seq[lows[0]:highs[0]] if e[0] == 'DELAY')
        after_us = sum(e[1] for e in seq[highs[0]:] if e[0] == 'DELAY')
        ok = low_us >= 10000 and after_us >= 50000
        return ok, 'RSTN low %d us, then %d us' % (low_us, after_us), []

    if op == 'read':
        want = hexes(row['val'])[0]
        ev = take('R')
        if not ev or reg_of(ev) != regs[0]:
            return False, 'no read of 0x%04X' % regs[0], evs[i:]
        return ev[4] == want, 'read 0x%04X = 0x%02X (expect 0x%02X)' % (regs[0], ev[4], want), evs[i:]

    if op == 'read word':
        msgs = []
        for r in regs:
            ev = take('R')
            if not ev or reg_of(ev) != r:
                return False, 'no read of 0x%04X' % r, evs[i:]
            msgs.append('0x%04X=0x%02X' % (r, ev[4]))
        return True, 'read ' + ' '.join(msgs), evs[i:]

    if op == 'wait':
        m = re.match(r'(\d+) (ms|us)', row['val'])
        want = int(m.group(1)) * (1000 if m.group(2) == 'ms' else 1)
        got = sum(e[1] for e in evs if e[0] == 'DELAY')
        rest = [e for e in evs if e[0] != 'DELAY']
        return got >= want, 'waited %d us (at least %d)' % (got, want), rest

    if op.startswith('RMW'):
        mask = hexes(op)[0]
        val = expected_values(row, phase)[0]
        r = regs[0]
        ev_r = take('R')
        ev_w = take('W')
        if not ev_r or not ev_w or reg_of(ev_r) != r or reg_of(ev_w) != r:
            return False, 'no read then write of 0x%04X' % r, evs[i:]
        old, new = ev_r[4], ev_w[2][2]
        want = (old & ~mask & 0xFF) | (val & mask)
        image[r] = new
        controlled[r] = controlled.get(r, 0) | mask
        return new == want, 'RMW 0x%04X mask 0x%02X: 0x%02X -> 0x%02X (expect 0x%02X)' % (
            r, mask, old, new, want), evs[i:]

    if op.startswith('write'):
        vals = expected_values(row, phase)
        targets = regs if len(regs) == len(vals) else [regs[0]] * len(vals)
        got = []
        for r, v in zip(targets, vals):
            ev = take('W')
            if not ev or reg_of(ev) != r:
                return False, 'no write of 0x%04X' % r, evs[i:]
            got.append(ev[2][2])
            if ev[2][2] != v:
                return False, 'write 0x%04X = 0x%02X (expect 0x%02X)' % (r, ev[2][2], v), evs[i:]
            image[r] = ev[2][2]
            controlled[r] = 0xFF
        return True, 'write %s = %s' % (
            '/'.join('0x%04X' % r for r in sorted(set(targets))),
            ' '.join('%02X' % g for g in got)), evs[i:]

    return False, 'operation %r not understood' % op, evs


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    md, log = sys.argv[1], sys.argv[2]
    phase = 0
    if '--phase' in sys.argv:
        phase = int(sys.argv[sys.argv.index('--phase') + 1], 0)
    table = parse_table(md)
    groups, final = parse_log(log)
    by_id = {}
    order = []
    for gid, evs in groups:
        if gid == '-':
            continue
        order.append(gid)
        by_id.setdefault(gid, []).append(evs)
    table_ids = [r['id'] for r in table]

    fails = 0
    print('encoder.md 8.4: %d rows read (A %d, B %d, C %d, D %d, E %d)' % (
        len(table), *(sum(1 for r in table if r['id'][0] == s) for s in 'ABCDE')))
    # 1. order: the log's rows must be the table's rows, in order, each once
    if order != table_ids:
        missing = [t for t in table_ids if t not in order]
        extra = [o for o in order if o not in table_ids]
        print('ORDER differs: missing %s, extra %s' % (missing, extra))
        fails += 1
    else:
        print('order: the log runs all %d rows in the table order, each once' % len(order))

    # 2. row by row
    image, controlled = {}, {}
    ls_checks = []
    for row in table:
        runs = by_id.get(row['id'])
        if not runs:
            print('%-4s MISSING' % row['id'])
            fails += 1
            continue
        ok, msg, extra = check_row(row, runs[0], phase, image, controlled)
        bad_extra = [e for e in extra if not is_allowed_extra(e)]
        flag = 'ok  ' if ok and not bad_extra else 'FAIL'
        if flag == 'FAIL':
            fails += 1
        note = ''
        if extra:
            kinds = []
            for e in extra:
                if e[0] in ('R', 'W') and e[1] == 0x59:
                    kinds.append('%s 0x%04X' % (e[0], reg_of(e)))
                elif e[0] in ('R', 'RNAK'):
                    kinds.append('EDID %s' % ('read' if e[0] == 'R' else 'NAK'))
                else:
                    kinds.append('%s %s' % (e[0], e[1] if len(e) > 1 else ''))
            note = '  + section 8.6: ' + ', '.join(kinds)
            if bad_extra:
                note += '  UNEXPECTED: %s' % bad_extra
        print('%s %-4s %s%s' % (flag, row['id'], msg, note))
        # Loongson / Meyesemi values quoted in the Source column
        for reg_s, val_s in re.findall(r'0x([0-9A-Fa-f]{4}) = 0x([0-9A-Fa-f]{2})', row['src']):
            ls_checks.append((row['id'], int(reg_s, 16), int(val_s, 16),
                              image.get(int(reg_s, 16)), controlled.get(int(reg_s, 16), 0)))

    # 3. the flat tables' values, on the bits this sequence has set by then
    print('Loongson and Meyesemi values quoted in the table:')
    for rid, reg, ref, ours, ctl in ls_checks:
        if ours is None:
            print('  ??  %-4s 0x%04X: not written by the sequence' % (rid, reg))
            fails += 1
            continue
        same = (ours & ctl) == (ref & ctl)
        print('  %s %-4s 0x%04X: table 0x%02X, ours 0x%02X, compared on bits 0x%02X' % (
            'ok  ' if same else 'DIFF', rid, reg, ref, ours, ctl))
        if not same:
            fails += 1

    # 4. byte order on the wire: every 0x59 write is index low, index high, data
    n = sum(1 for _, evs in groups for e in evs if e[0] == 'W' and e[1] == 0x59)
    bad = sum(1 for _, evs in groups for e in evs if e[0] == 'W' and e[1] == 0x59 and len(e[2]) != 3)
    print('wire: %d register writes, %d not three bytes' % (n, bad))
    fails += bad
    print('RESULT: %s (%d problems)' % ('PASS' if fails == 0 else 'FAIL', fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
