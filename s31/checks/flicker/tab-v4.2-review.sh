# tab.sh LIST: per run, the pictures and the new count of black, and the END line against the
# same run of the fix agent (~/flicker/fix/h/runs) or of the review (~/flicker/review/h/runs)
set -u
A=~/flicker/apply42; H=$A/h
endline() { grep -a "END" $1/log.txt 2>/dev/null | sed 's/ wall=[0-9.]* s//'; }
other() {   # my run name -> the earlier run of the same configuration
  case $1 in
    a42-dosfix) echo ~/flicker/fix/h/runs/v42-dosfix ;;
    a42-*) echo ~/flicker/fix/h/runs/v42-${1#a42-} ;;
    a41-*) echo ~/flicker/fix/h/runs/w41-${1#a41-} ;;
    a39-*) echo ~/flicker/fix/h/runs/v39-${1#a39-} ;;
    d4*) echo ~/flicker/review/h/runs/$1 ;;
    c42-gi-rel) echo ~/flicker/fix/h41/runs/gi18-v42 ;;
    *) echo none ;;
  esac
}
printf '%-20s %6s %5s %5s | %5s %8s %6s | %8s %8s | %4s %8s | %s\n' run pics diff blank around vb_px vb_mid memset mem_mid clr flash "END against the earlier run"
while read -r l; do
  case "$l" in ''|'#'*) continue ;; esac
  n=${l%% *}; d=$H/runs/$n
  [ -f $d/log.txt ] || { echo "$n: no log"; continue; }
  p=$(grep -a 'board: [0-9]* pictures' $d/log.txt | sed 's/.*board: \([0-9]*\) pictures; \([0-9]*\) checked against the reference: \([0-9]*\) differ.*/\1 \3/')
  c=$(grep -a 'board: clears in' $d/log.txt | sed 's/.*clears in \([0-9]*\) steps, \([0-9]*\) of them over a picture (flash, \([0-9]*\) pixels.*blank pictures \([0-9]*\).*/\1 \2 \3 \4/')
  b=$(grep -a 'black between two pictures' $d/log.txt | sed 's/.*around a picture (VGA_CLEAR_AROUND) in \([0-9]*\) steps; vga_black() made \([0-9]*\) pixels black in [0-9]* steps, \([0-9]*\) of them.*memset() made \([0-9]*\) panel pixels black, \([0-9]*\) of them.*/\1 \2 \3 \4 \5/')
  set -- $p; pics=${1:--}; dif=${2:--}
  set -- $c; clr=${2:--}; fl=${3:--}; blank=${4:--}
  set -- $b; ar=${1:--}; vb=${2:--}; vbm=${3:--}; mem=${4:--}; memm=${5:--}
  o=$(other $n); e="-"
  if [ "$o" != none ]; then
    if [ -f $o/log.txt ]; then
      if [ "$(endline $d)" = "$(endline $o)" ]; then e="same as $(basename $(dirname $(dirname $o)))/$(basename $o)"; else e="DIFFERS from $(basename $o)"; fi
    else e="(no earlier run $o)"; fi
  fi
  printf '%-20s %6s %5s %5s | %5s %8s %6s | %8s %8s | %4s %8s | %s\n' $n $pics $dif $blank $ar $vb $vbm $mem $memm $clr $fl "$e"
done < $1
