# file: pc
# workload: vbe
# a lost mark: a 32-bit store into the VBE linear frame buffer reaches video memory but marks no block, so the picture is not drawn again there (what a fast path for the frame buffer must not do)
/^static void iomem_write32/,/^}/s/LFB_WRITTEN(pc, addr, 4);/(void) 0;/
