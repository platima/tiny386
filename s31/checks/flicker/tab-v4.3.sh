# tab43.sh LIST: per run of my flicker list, the pictures checked and differing, blank pictures, the
# count of black (clears around a picture, pixels vga_black() made black and of them not black once
# the picture was done, the same for a memset()), and the END line against apply42's v4.2 run of the
# same configuration (~/flicker/apply42/h/runs: a42-, d42-, c42-, e42-)
set -u
A=~/v43/merge; H=$A/fh
endline() { grep -a "\] END" $1/log.txt 2>/dev/null | sed 's/ wall=[0-9.]* s//'; }
other() {
  case $1 in
    a43-*) echo ~/flicker/apply42/h/runs/a42-${1#a43-} ;;
    d43-*) echo ~/flicker/apply42/h/runs/d42-${1#d43-} ;;
    c43-*) echo ~/flicker/apply42/h/runs/c42-${1#c43-} ;;
    e43-*) echo ~/flicker/apply42/h/runs/e42-${1#e43-} ;;
    *) echo none ;;
  esac
}
printf '%-20s %6s %5s %5s | %5s %8s %6s | %8s %8s | %s\n' run pics diff blank around vb_px vb_mid memset mem_mid "END against apply42's v4.2 run"
same=0; n=0
while read -r l; do
  case "$l" in ''|'#'*) continue ;; esac
  r=${l%% *}; d=$H/runs/$r
  [ -f $d/log.txt ] || { echo "$r: no log"; continue; }
  p=$(grep -a 'board: [0-9]* pictures' $d/log.txt | sed 's/.*board: \([0-9]*\) pictures; \([0-9]*\) checked against the reference: \([0-9]*\) differ.*/\1 \3/')
  c=$(grep -a 'board: clears in' $d/log.txt | sed 's/.*blank pictures \([0-9]*\).*/\1/')
  b=$(grep -a 'black between two pictures' $d/log.txt | sed 's/.*around a picture (VGA_CLEAR_AROUND) in \([0-9]*\) steps; vga_black() made \([0-9]*\) pixels black in [0-9]* steps, \([0-9]*\) of them.*memset() made \([0-9]*\) panel pixels black, \([0-9]*\) of them.*/\1 \2 \3 \4 \5/')
  set -- $p; pics=${1:--}; dif=${2:--}
  blank=${c:--}
  set -- $b; ar=${1:--}; vb=${2:--}; vbm=${3:--}; mem=${4:--}; memm=${5:--}
  o=$(other $r); e=-
  if [ -f $o/log.txt ]; then
    n=$((n+1))
    if [ "$(endline $d)" = "$(endline $o)" ]; then e="same as $(basename $o)"; same=$((same+1)); else e="DIFFERS from $(basename $o)"; fi
  else e="(no earlier run $o)"; fi
  printf '%-20s %6s %5s %5s | %5s %8s %6s | %8s %8s | %s\n' $r $pics $dif $blank $ar $vb $vbm $mem $memm "$e"
done < $1
echo "END lines: $same of $n the same as apply42's v4.2 run"
