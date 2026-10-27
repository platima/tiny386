# file: i386_fast
# workload: b1
# needs: fast
# the fast interpreter fuses CMP or TEST with a JZ or JNZ after it where tiny386 would not, at the first instruction of a newly cached page (the bug tests/host/cpu/test_fast_cpu.c found in the port): the jump joins the CMP's step
s/FAST_ITER() != nofold_it \&\& \\/1 \&\& \\/
