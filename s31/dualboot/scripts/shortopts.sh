# Offline check that the short option forms used in FLASH.txt and the .bat
# files parse (merge-bin shares write-flash's flash options; no port is opened).
W=$HOME/tiny386-idf-scratch/dualboot
R=$W/r2
mkdir -p $R/shortopts && cd $R/shortopts || exit 1
E=/home/owner/.espressif-upstream/python_env/idf6.2_py3.11_env/lib/python3.11/site-packages/esptool
{
echo "### 11. short option forms, checked offline"
echo "== defaults of the global reset options (esptool/__init__.py)"
grep -n -A6 '"--before"' $E/__init__.py | grep -n -i -E "before|default" | head -6
grep -n -A6 '"--after"' $E/__init__.py | grep -n -i -E "after|default|-a" | head -6
env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH=$HOME/esp/esp-idf IDF_TOOLS_PATH=$HOME/.espressif-upstream W=$W bash -c '. $HOME/esp/esp-idf/export.sh >/dev/null 2>&1
echo "== esptool -c esp32s31 -b 921600 merge-bin -o m.bin -fm keep -ff keep -fs keep 0x2000 <dual bootloader> 0x8000 <dual table> 0xe20000 <v2 tiny386>"
esptool -c esp32s31 -b 921600 merge-bin -o m.bin -fm keep -ff keep -fs keep 0x2000 $W/dual/build/bootloader/bootloader/bootloader.bin 0x8000 $W/dual/build/bootloader/partition_table/partition-table.bin 0xe20000 $W/inputs/v2/tiny386.bin; echo "exit $?"
python3 - <<PY
b = open("m.bin","rb").read()
bl = open("$W/dual/build/bootloader/bootloader/bootloader.bin","rb").read()
print("merged length 0x%x (from 0x0); bootloader bytes at 0x2000 unchanged by -fm/-ff/-fs keep: %s" % (len(b), b[0x2000:0x2000+len(bl)] == bl))
PY
echo "== the same with a wrong flash-mode value, to see the parser reject it"
esptool -c esp32s31 merge-bin -o m2.bin -fm kept 0x2000 $W/dual/build/bootloader/bootloader/bootloader.bin 2>&1 | grep -i -E "invalid|error" | head -3; echo "exit ${PIPESTATUS[0]}"
'
rm -f m.bin m2.bin
} > $R/11-short-options.txt 2>&1
cat $R/11-short-options.txt
