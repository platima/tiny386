# file: i386
# workload: b1
# an off-by-one count: REP MOVS copies one element too few when its chunk ends at the end of the destination's page and more is left to copy (the registers move on as if all were copied)
/#define MOVS_helper2/,/#define MOVS_helper(BIT)/s/for (uword i = 0; i <= count - 1; i++) {/for (uword i = 0; i + (count == (uword) countd \&\& count < cx) <= count - 1; i++) {/
