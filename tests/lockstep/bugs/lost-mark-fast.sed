# file: i386_fast
# workload: t386mon800
# needs: fast
# a lost mark in the fast interpreter's own store into the frame buffer (v5.1): a MOV through a page's FAST_TAG_LFB write tag (lfb_st in i386_fast.h) stores its bytes but marks no block, so the drawing would leave the old picture there
/^lfb_st: {/,/NEXT; }/{/lfb_marked(/d}
