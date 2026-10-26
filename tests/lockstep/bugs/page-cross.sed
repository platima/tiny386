# file: i386
# workload: b1
# a missed page-cross: a 32-bit access that crosses a page boundary with paging on is not split, so its last bytes come from (or go to) the physical page after the first one instead of the page the next linear page maps to
s/if ((laddr \& 0xfff) > 0x1000 - size) {/if ((laddr \& 0xfff) > 0x1000 - size \&\& size != 4) {/
