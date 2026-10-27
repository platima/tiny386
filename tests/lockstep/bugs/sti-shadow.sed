# file: i386
# workload: b1
# needs: tiny386
# interrupt timing: after STI the CPU runs two more instructions before it takes a waiting interrupt, not one
s/STEP_HOLD(1, if (cpu->intr || stepcount < 2) stepcount = 2);/STEP_HOLD(1, if (cpu->intr || stepcount < 2) stepcount = 3);/
