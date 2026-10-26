# file: i386
# workload: b1
# a wrong flag: AF after a 32-bit DEC is set when the low nibble of the result is 0xE, not 0xF (nothing in the boot reads it, so Windows still reaches the same desktop)
s/return (cpu->cc.dst \& 0xf) == 0xf;/return (cpu->cc.op == CC_DEC32) ? ((cpu->cc.dst \& 0xf) == 0xe) : ((cpu->cc.dst \& 0xf) == 0xf);/
