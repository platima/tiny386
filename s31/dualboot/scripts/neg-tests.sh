# How esptool image-info reports a damaged or wrong file (for the .bat guard).
# Runs inside ESP-IDF's environment; writes only under $R/neg.
mkdir -p "$R/neg"; cd "$R/neg"
T=$W/inputs/v2/tiny386.bin
cp "$T" good.bin
python3 -c '
import sys
b = bytearray(open(sys.argv[1], "rb").read())
b[0x100000] ^= 0xFF
open("flipped.bin", "wb").write(b)
open("truncated.bin", "wb").write(b[:len(b)//2])
' "$T"
cp "$W/inputs/v2/partition_table/partition-table.bin" table.bin
: > empty.bin
for f in good.bin flipped.bin truncated.bin table.bin empty.bin missing.bin; do
  echo "=================== $f"
  esptool --chip esp32s31 image-info "$f" > out.txt 2>&1; rc=$?
  echo "exit code $rc"
  grep -n -i -E "invalid|error|chip id|validation hash|checksum|warning" out.txt | head -8
  printf 'findstr-style "(invalid)" present: '; grep -q "(invalid)" out.txt && echo yes || echo no
done
rm -f good.bin flipped.bin truncated.bin table.bin empty.bin out.txt
