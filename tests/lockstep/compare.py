#!/usr/bin/env python3
"""tests/lockstep: compare what the hook wrote for two runs.

    python3 -I compare.py records REF CAND [--json OUT] [--ignore k,k]
    python3 -I compare.py children REF CAND [--json OUT] [--ignore k,k]

REF and CAND are the hook's directories (RUNDIR/ls).  `records` compares
records.txt (record by record, in order) and irq.txt (interrupt by
interrupt), and says where they first differ; `children` compares
children.txt (the state after each prefix of one step) and names the first
instruction whose result differs.  A key that starts with _ is shown but
never compared.  Exit status 0: the same; 1: they differ; 2: error.

Offered under the licence of tiny386 (BSD 3-clause).
"""
import json
import os
import subprocess
import sys
import tempfile

FLAG_BITS = [(0, 'CF'), (2, 'PF'), (4, 'AF'), (6, 'ZF'), (7, 'SF'), (8, 'TF'), (9, 'IF'),
             (10, 'DF'), (11, 'OF'), (12, 'IOPL0'), (13, 'IOPL1'), (14, 'NT'), (16, 'RF'),
             (17, 'VM'), (18, 'AC'), (21, 'ID')]
PIC_NAMES = ['last_irr', 'irr', 'imr', 'isr', 'priority_add', 'irq_base', 'read_reg_select',
             'poll', 'special_mask', 'init_state', 'auto_eoi', 'rotate_on_auto_eoi',
             'special_fully_nested', 'init4', 'single_mode']
PIT_NAMES = ['count', 'latched_count', 'count_latched', 'status_latched', 'status',
             'read_state', 'write_state', 'write_latch', 'rw_mode', 'mode', 'bcd', 'gate',
             'reload_pending', 'stopped_count', 'count_load_time', 'last_irq_count', 'irq']
SEG_NAMES = ['sel', 'base', 'limit', 'flags']
DK_NAMES = ['reads', 'writes', 'bytes read', 'bytes written', 'seeks', 'flushes']
REGS = ['eip', 'eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi', 'fl']
SEGS = ['es', 'cs', 'ss', 'ds', 'fs', 'gs', 'ldt', 'tr']
# never compared in `children`: shown instead (the memory runs and the next
# instruction's bytes follow from what is compared)
CHILD_SHOWN = {'mem', 'vmem', 'code', 'n'}


def parse(line):
    kind, _, rest = line.rstrip('\n').partition(' ')
    d = {'_kind': kind}
    for kv in rest.split(' '):
        if '=' in kv:
            k, v = kv.split('=', 1)
            d[k] = v
    return d


def load(path, kinds):
    out = []
    if not os.path.exists(path):
        return out
    with open(path, errors='replace') as f:
        for line in f:
            if line[:1] in kinds:
                out.append(parse(line))
    return out


def compared(d, ignore, extra=()):
    return [k for k in d if not k.startswith('_') and k not in ignore and k not in extra]


def differing(a, b, ignore, extra=()):
    keys = compared(a, ignore, extra)
    keys += [k for k in compared(b, ignore, extra) if k not in a]
    if a.get('_kind') != b.get('_kind'):
        keys.append('_kind')
    return [k for k in keys if a.get(k) != b.get(k)]


def first_difference(ra, rb, ignore, extra=('i',)):
    n = min(len(ra), len(rb))
    for j in range(n):
        dk = differing(ra[j], rb[j], ignore, extra)
        if dk:
            return j, dk
    if len(ra) != len(rb):
        return n, ['(one run has more records: ref %d, cand %d)' % (len(ra), len(rb))]
    return None, []


def explain(key, a, b):
    """lines that say what differs inside one field"""
    va, vb = a.get(key, '(none)'), b.get(key, '(none)')
    out = ['  %-6s ref %s' % (key, va), '  %-6s cand %s' % ('', vb)]
    try:
        if key == 'fl':
            x, y = int(va, 16), int(vb, 16)
            bits = [n for b_, n in FLAG_BITS if (x ^ y) >> b_ & 1]
            out.append('         flags that differ: %s (ref %s, cand %s)' % (
                ' '.join(bits),
                ' '.join('%s=%d' % (n, x >> b_ & 1) for b_, n in FLAG_BITS if (x ^ y) >> b_ & 1),
                ' '.join('%s=%d' % (n, y >> b_ & 1) for b_, n in FLAG_BITS if (x ^ y) >> b_ & 1)))
        elif key in SEGS:
            pa, pb = va.split(':'), vb.split(':')
            out.append('         differs in: %s' % ', '.join(
                SEG_NAMES[i] for i in range(4) if pa[i] != pb[i]))
        elif key == 'rr':
            pa, pb = va.split(','), vb.split(',')
            out.append('         RAM that differs, by MiB: %s' % ', '.join(
                '%d-%d MiB' % (i, i + 1) for i in range(min(len(pa), len(pb))) if pa[i] != pb[i]))
        elif key == 'pic':
            x, y = bytes.fromhex(va), bytes.fromhex(vb)
            out.append('         differs in: %s' % ', '.join(
                '%s.%s %02x/%02x' % ('master' if i < 15 else 'slave', PIC_NAMES[i % 15], x[i], y[i])
                for i in range(min(len(x), len(y))) if x[i] != y[i]))
        elif key == 'pit':
            pa = [w for ch in va.split(',') for w in ch.split('.')]
            pb = [w for ch in vb.split(',') for w in ch.split('.')]
            out.append('         differs in: %s' % ', '.join(
                'ch%d.%s %s/%s' % (i // 17, PIT_NAMES[i % 17], pa[i], pb[i])
                for i in range(min(len(pa), len(pb))) if pa[i] != pb[i]))
        elif key == 'dk':
            pa, pb = va.split(':'), vb.split(':')
            out.append('         disk counters that differ: %s' % ', '.join(
                '%s %s/%s' % (DK_NAMES[i], pa[i], pb[i]) for i in range(min(len(pa), len(pb)))
                if pa[i] != pb[i]))
    except (ValueError, IndexError):
        pass
    return out


def regs_line(d):
    return ' '.join('%s=%s' % (k, d.get(k, '?')) for k in REGS) + \
        '  cs=%s ss=%s ds=%s es=%s cpl=%s c16=%s' % (
            d.get('cs', '?').split(':')[0], d.get('ss', '?').split(':')[0],
            d.get('ds', '?').split(':')[0], d.get('es', '?').split(':')[0], d.get('cpl', '?'),
            d.get('c16', '?'))


def where(d):
    return 'step %s, cycle %s, %s:%s' % (d.get('step'), d.get('cyc'),
                                         d.get('cs', '?').split(':')[0], d.get('eip'))


def disasm(code, c16):
    if not code or code == '-':
        return '(bytes not readable)'
    try:
        raw = bytes.fromhex(code)
    except ValueError:
        return '(bad bytes)'
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(raw)
        name = f.name
    try:
        opt = 'intel,i8086' if c16 == '1' else 'intel'
        r = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', opt, name],
                           capture_output=True, text=True, timeout=20)
        for line in r.stdout.splitlines():
            s = line.strip()
            if s.startswith('0:'):
                parts = s.split('\t')
                return '%s   (%s)' % (' '.join(parts[2:]).strip() if len(parts) > 2 else s,
                                      parts[1].strip() if len(parts) > 1 else '')
        return '(objdump gave nothing)'
    except (OSError, subprocess.SubprocessError):
        return '(no objdump)'
    finally:
        os.unlink(name)


def cmd_records(ref, cand, ignore, out):
    ra = load(os.path.join(ref, 'records.txt'), 'RF')
    rb = load(os.path.join(cand, 'records.txt'), 'RF')
    ia = load(os.path.join(ref, 'irq.txt'), 'I')
    ib = load(os.path.join(cand, 'irq.txt'), 'I')
    res = {'records': [len(ra), len(rb)], 'irqs': [len(ia), len(ib)]}
    if not ra or not rb:
        print('no records in %s' % (ref if not ra else cand))
        res['verdict'] = 'error'
        return 2, res
    j, dk = first_difference(ra, rb, ignore)
    ji, dki = first_difference(ia, ib, ignore, extra=())
    lines = []
    lines.append('records: ref %d, cand %d; interrupts: ref %d, cand %d' % (
        len(ra), len(rb), len(ia), len(ib)))
    fa, fb = ra[-1], rb[-1]
    lines.append('last record: ref %s (%s), cand %s (%s)' % (
        where(fa), fa.get('_why', fa['_kind']), where(fb), fb.get('_why', fb['_kind'])))
    if j is None and ji is None:
        res['verdict'] = 'same'
        lines.append('SAME: every record and every interrupt agree')
        print('\n'.join(lines))
        return 0, res
    res['verdict'] = 'differ'
    if ji is not None:
        lines.append('')
        lines.append('first interrupt that differs: number %d' % (ji + 1))
        for name, lst in (('ref ', ia), ('cand', ib)):
            if ji < len(lst):
                d = lst[ji]
                lines.append('  %s cycle %s vector %s at %s:%s fl=%s (step %s, time %s us)' % (
                    name, d.get('cyc'), d.get('vec'), d.get('cs'), d.get('eip'), d.get('fl'),
                    d.get('step'), d.get('ut')))
            else:
                lines.append('  %s (no more interrupts)' % name)
        if ji > 0:
            d = ia[ji - 1]
            lines.append('  the interrupt before agrees: cycle %s vector %s' % (
                d.get('cyc'), d.get('vec')))
        res['irq'] = {'index': ji, 'ref': ia[ji] if ji < len(ia) else None,
                      'cand': ib[ji] if ji < len(ib) else None}
    if j is not None:
        lines.append('')
        a = ra[j] if j < len(ra) else None
        b = rb[j] if j < len(rb) else None
        prev = ra[j - 1] if j > 0 else None
        lines.append('first record that differs: number %d' % j)
        if prev:
            lines.append('  the record before agrees: %s' % where(prev))
        else:
            lines.append('  (the very first record differs)')
        for name, d in (('ref ', a), ('cand', b)):
            lines.append('  %s %s' % (name, where(d) if d else '(no record: the run ended)'))
        if a and b:
            lines.append('  fields that differ: %s' % ' '.join(dk))
            for k in dk:
                if k in ('_kind',):
                    continue
                lines.extend(explain(k, a, b))
        cyc_a = int(prev['cyc']) if prev else 0
        cyc_b = max(int(d['cyc']) for d in (a, b) if d)
        res['first'] = {'index': j, 'keys': dk,
                        'anchor_cycle': cyc_a,
                        'anchor_step': int(prev['step']) if prev else -1,
                        'end_cycle': cyc_b,
                        'ref': a, 'cand': b, 'prev': prev}
        lines.append('  window to look closer: cycles %d to %d' % (cyc_a, cyc_b))
    print('\n'.join(lines))
    return 1, res


def cmd_children(ref, cand, ignore, out):
    ca = load(os.path.join(ref, 'children.txt'), 'C')
    cb = load(os.path.join(cand, 'children.txt'), 'C')
    res = {'children': [len(ca), len(cb)]}
    if not ca or not cb:
        print('no children in %s' % (ref if not ca else cand))
        res['verdict'] = 'error'
        return 2, res
    by_a = {int(d['m']): d for d in ca}
    by_b = {int(d['m']): d for d in cb}
    ms = sorted(set(by_a) | set(by_b))
    lines = ['step %d (after %s steps) split into %d prefixes (ref), %d (cand); child m ran the '
             'first m instructions of the step' % (int(ca[0].get('step', -1)) + 1, ca[0].get('step'),
                                                   len(ca), len(cb))]
    first = None
    for m in ms:
        a, b = by_a.get(m), by_b.get(m)
        if a is None or b is None:
            first = (m, ['(missing child)'])
            break
        dk = differing(a, b, ignore, CHILD_SHOWN)
        if dk:
            first = (m, dk)
            break
    if first is None:
        res['verdict'] = 'same'
        lines.append('SAME: every prefix of the step agrees (state, memory, ports, interrupts)')
        last_a, last_b = by_a[ms[-1]], by_b[ms[-1]]
        lines.append('  the step reached cycle %s in both' % last_a.get('cyc'))
        print('\n'.join(lines))
        return 0, res
    m, dk = first
    res['verdict'] = 'differ'
    a, b = by_a.get(m), by_b.get(m)
    before = by_a.get(m - 1) if m > 0 else None
    res['first'] = {'m': m, 'keys': dk, 'ref': a, 'cand': b, 'before': before}
    lines.append('')
    if m == 0:
        lines.append('the state at the start of the step already differs (no instruction run)')
    else:
        if a and before and int(a.get('cyc', 0)) == int(before.get('cyc', 0)):
            lines.append('NOTE: in the reference this prefix ran no further instruction than '
                         'the one before (the step ended, or the CPU halted)')
        lines.append('first instruction whose result differs: instruction %d of step %s' % (
            m, (int(a['step']) + 1) if a and 'step' in a else '?'))
        if before:
            lines.append('  it is at %s:%s, cycle %s -> %s' % (
                before.get('cs', '?').split(':')[0], before.get('eip'), before.get('cyc'),
                int(before.get('cyc', 0)) + 1))
            lines.append('  bytes %s' % before.get('code'))
            lines.append('  %s' % disasm(before.get('code'), before.get('c16')))
            lines.append('  before (both): %s' % regs_line(before))
            if before.get('irqn') != (a or {}).get('irqn') or before.get('irqn') != (b or {}).get('irqn'):
                lines.append('  an interrupt was taken here: irqn %s -> ref %s, cand %s' % (
                    before.get('irqn'), (a or {}).get('irqn'), (b or {}).get('irqn')))
    for name, d in (('ref ', a), ('cand', b)):
        if d is None:
            lines.append('  %s (no child)' % name)
        elif 'dead' in d:
            lines.append('  %s the child died: %s' % (name, d['dead']))
        else:
            lines.append('  after %s: %s (cycle %s)' % (name, regs_line(d), d.get('cyc')))
    if a and b and 'dead' not in a and 'dead' not in b:
        if 'cyc' in dk:
            lines.append('  NOTE: this prefix ran a different number of instructions in the two '
                         'cores (ref to cycle %s, cand to %s): they count instructions, or end a '
                         'step (HLT, an interrupt after IRET or POPF, the instruction after STI '
                         'or MOV SS), differently here' % (a.get('cyc'), b.get('cyc')))
        lines.append('  fields that differ: %s' % ' '.join(dk))
        for k in dk:
            lines.extend(explain(k, a, b))
        for key, label in (('mem', 'RAM'), ('vmem', 'video memory')):
            if a.get(key) != b.get(key):
                sa = set(a.get(key, '-').split(','))
                sb = set(b.get(key, '-').split(','))
                lines.append('  %s written since the step began (hex address:old>new), %d runs '
                             'in ref, %d in cand (at most 24 are listed); where they differ:' % (
                                 label, len(sa), len(sb)))
                lines.append('    only in ref:  %s' % (','.join(sorted(sa - sb)) or '-'))
                lines.append('    only in cand: %s' % (','.join(sorted(sb - sa)) or '-'))
    print('\n'.join(lines))
    return 1, res


def main(argv):
    if len(argv) < 4 or argv[1] not in ('records', 'children'):
        print(__doc__)
        return 2
    ignore = set()
    out = None
    rest = argv[4:]
    while rest:
        if rest[0] == '--json' and len(rest) > 1:
            out = rest[1]
            rest = rest[2:]
        elif rest[0] == '--ignore' and len(rest) > 1:
            ignore |= set(k for k in rest[1].split(',') if k)
            rest = rest[2:]
        else:
            print('unknown argument %s' % rest[0])
            return 2
    fn = cmd_records if argv[1] == 'records' else cmd_children
    rc, res = fn(argv[2], argv[3], ignore, out)
    if out:
        with open(out, 'w') as f:
            json.dump(res, f, indent=1)
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
