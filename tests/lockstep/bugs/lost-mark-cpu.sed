# file: i386
# workload: vbe
# a lost mark in v5.0's own path: a 32-bit store into the VBE linear frame buffer, made by i386.c itself, reaches video memory but marks no block (lost-mark's bug where v5.0 stores)
/^static inline void lfb_marked(/,/^}/s/if (__atomic_load_n(cpu->cb.lfb_marks_on, __ATOMIC_RELAXED)) {/if (n != 4 \&\& __atomic_load_n(cpu->cb.lfb_marks_on, __ATOMIC_RELAXED)) {/
