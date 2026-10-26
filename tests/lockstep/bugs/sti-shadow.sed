# file: i386
# workload: b1
# interrupt timing: after STI the CPU runs two more instructions before it takes a waiting interrupt, not one
s/if (cpu->intr || stepcount < 2) stepcount = 2;/if (cpu->intr || stepcount < 2) stepcount = 3;/
