# file: i386_fast
# workload: b1
# needs: fast
# the fast interpreter does not bring the instruction count up to date before a port or MMIO access (SYNC_IN() left out): the timer and the clock read a count a few instructions short
s/^\tSYNC_IN();$/\t(void) 0;/
