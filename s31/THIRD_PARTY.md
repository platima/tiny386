# Third-party code on this branch

tiny386 itself is hchunhui's (Chunhui He), under the BSD 3-clause licence (`LICENSE` at the top of
the tree), and the changes made to it on this branch are offered under the same licence. This note
lists the code here that came from another project, with its notice. Written for v5.0
(2026-10-27).


## tab5-pc's fast interpreter (v5.0)

**What.** `i386_fast.h` is Yejun Yang's fast interpreter from tab5-pc, his port of tiny386 to the
M5Stack Tab5 (github.com/yejun/tab5-pc). It was taken from `firmware/tab5_386/src/t386_fast.h` at
commit `98a442e` (the commit `research\JIT-OPTIONS-2026-10-24.md` measured; release 1.0.0 is of
2026-10-22), with the `cpui386_step()` of that file folded into `i386.c`'s own and the hook of his
patch 0010 in `tlb_clear()`. tab5-pc's translator (`t386_jit.h`, `t386_fpu_jit.h`), its other
patches and its own paths into video memory were not taken. Yejun Yang also wrote tiny386's PRs
#40 to #42, which this branch has.

**Licence.** tab5-pc's own code is under the MIT licence, Copyright (c) 2026 Yejun Yang. The parts
of `t386_fast.h` built on tiny386's internal definitions, and the `cpui386_step()` adapted from
tiny386's, are Copyright (c) 2024-2025 Chunhui He, under tiny386's BSD 3-clause licence (tab5-pc's
`LICENSE` and the file's own header say both). `i386_fast.h` keeps both notices and the MIT text at
its top. The changes made to it on this branch are offered under the same terms.

**What was changed here**, so that the guest sees exactly what tiny386 alone shows it
(`FAST-CPU.md` 3 has each, `BUILD-NOTES.md` 33 the v5.0 integration):

* the count of instructions kept exact at every port and MMIO access and at every hand-over to
  tiny386, and each step ended where tiny386's `cpu_exec1()` ends it;
* tiny386's CMP and JZ fusion done where tiny386 does it, and instruction bytes read only from the
  page in tiny386's own fetch cache;
* its TLB kept as a subset of tiny386's, with the dirty bit set on every store;
* video memory and other MMIO through tiny386's own `load8()` to `store32()`, so the dirty marks of
  this branch's drawing are kept, and REP STOS and REP MOVS into video memory through tiny386's
  v5.0 loops (`lfb_stos()`, `lfb_movs()`, the VGA window's own calls);
* wrapping 16-bit stack slots, INT's ESP, BT with an immediate beyond the operand, ARPL's write and
  failed segment loads done as tiny386 does them.

**The MIT licence, as tab5-pc gives it:**

```
MIT License

Copyright (c) 2026 Yejun Yang

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```


## Elsewhere in the releases

* **The VGA BIOS** in the release packages is SeaBIOS's, under the GNU LGPL version 3; its source
  goes with it (`bios\` in the package).
* **ESP-IDF** and the components its manager fetches (`esp/dependencies.lock`) are Espressif's and
  their authors', under their own licences, mostly Apache 2.0; they are built into the images, not
  kept on this branch.
* **Windows 95 and its display driver** (VMDisp9x) are not part of this branch or its releases.
