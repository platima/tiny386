set -u
W=$HOME/tiny386-idf-scratch/dualboot
mkdir -p $W/esptool-help
cd $W/esptool-help
env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH=$HOME/esp/esp-idf IDF_TOOLS_PATH=$HOME/.espressif-upstream bash -c '. $HOME/esp/esp-idf/export.sh >/dev/null 2>&1
esptool version > version.txt 2>&1
esptool --help > main-help.txt 2>&1
esptool write-flash --help > write-flash-help.txt 2>&1
esptool image-info --help > image-info-help.txt 2>&1
esptool erase-region --help > erase-region-help.txt 2>&1
esptool read-flash --help > read-flash-help.txt 2>&1
'
cat version.txt; cat main-help.txt; cat write-flash-help.txt
