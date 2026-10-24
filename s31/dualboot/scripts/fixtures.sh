# image-info outputs to use as fixtures for testing the .bat subroutines
R=$HOME/tiny386-idf-scratch/dualboot/r2
mkdir -p $R/fixtures && cd $R/fixtures || exit 1
T=$HOME/tiny386-idf-scratch/dualboot/inputs/v2/tiny386.bin
python3 -c '
import sys
b = bytearray(open(sys.argv[1], "rb").read()); b[0x100000] ^= 0xFF
open("flipped.bin", "wb").write(b)' "$T"
env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH=$HOME/esp/esp-idf IDF_TOOLS_PATH=$HOME/.espressif-upstream T=$T bash -c '. $HOME/esp/esp-idf/export.sh >/dev/null 2>&1
esptool -c esp32s31 image-info "$T" > ii-good.txt 2>&1
esptool -c esp32s31 image-info flipped.bin > ii-invalid.txt 2>&1'
rm -f flipped.bin
wc -l ii-good.txt ii-invalid.txt
