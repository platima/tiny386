#!/bin/bash
# Builds bios.bin, and vgabios.bin beside it, for the cards of the S31
# boards' tiny386: SeaBIOS at 81ec9ec0 (twenty commits after release
# 1.17.0) with this tree's seabios/patch and seabios/config (tiny386's own
# patch and configuration, with v5.3's change to mathcp_setup()), then
# seabios-korvo.patch beside this script, the Linux route's BIOS fixes
# (esp32-s31-linux-release/tiny386/patches/seabios-korvo.patch, unchanged).
#
#   bash s31/bios/build-bios.sh WORKDIR
#
# This is the Linux route's tools/build-bios.sh with these changes only:
# the patches and the configuration from where they are in this tree; no
# UPSTREAM setting (tiny386's own build needs tiny386's own patch, which the
# Linux route's script keeps); WORKDIR is to be given; PATH is set to the
# system's folders, as WSL's holds Windows folders with spaces in their
# names; and the version string's time is 20261111_000000 for bios.bin
# (vgabios.bin keeps 20261019_000000, and so its bytes), each settable as
# SB_STAMP_BIOS and SB_STAMP_VGA.  With SB_STAMP_BIOS=20261019_000000 and
# v5.2's seabios/patch it builds the Linux route's bios.bin byte for byte.
#
# Everything is fetched and built inside WORKDIR; nothing outside it is
# written.  The results are WORKDIR/out/bios.bin, WORKDIR/out/vgabios.bin
# and WORKDIR/out/seabios-korvo-src.tar.xz, the source tree they were
# built from, patches applied.  README.txt beside this script says more.
#
# Settings, from the environment:
#   TOOLCHAIN  ubuntu (the default): the compiler tiny386's CI used, gcc
#              15.2.0-16ubuntu1 and binutils 2.46-3ubuntu2 of Ubuntu 26.04,
#              fetched from archive.ubuntu.com as packages, checked against
#              the checksums below, unpacked into WORKDIR and run from there
#              without root.  host: the system's own gcc and binutils; the
#              files then do the same, but are not the same bytes.
#
# It needs git, patch, make, python3, curl and xz, and for TOOLCHAIN=ubuntu
# dpkg-deb, patchelf and unshare (util-linux) with user namespaces allowed.
# Ubuntu's gcc calls its assembler as /usr/bin/x86_64-linux-gnu-as, which on
# Debian is Debian's.  So the build runs in a user and mount namespace of its
# own, where Ubuntu's assembler and linker are mounted over those two paths;
# nothing outside the namespace sees it, and no root is needed.
#
# The version string: SeaBIOS puts the time of the build and the host's name
# into it when its tree has changes, as it has here.  This script gives it a
# fixed time and name instead, so that every build gives the same bytes.
#
# SeaBIOS is under the GNU LGPL version 3, and so are the two patches.
set -eu

SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

# Inside the namespace: Ubuntu's assembler and linker where Ubuntu's gcc
# looks for them first, then the build
if [ "${1:-}" = --inside ]; then
    U=$2
    shift 2
    [ -e /usr/bin/x86_64-linux-gnu-as ] &&
        mount --bind "$U/usr/bin/x86_64-linux-gnu-as" /usr/bin/x86_64-linux-gnu-as
    [ -e /usr/bin/x86_64-linux-gnu-ld ] &&
        mount --bind "$U/usr/bin/x86_64-linux-gnu-ld.bfd" /usr/bin/x86_64-linux-gnu-ld
    exec "$@"
fi

# the system's folders (WSL's PATH has Windows folders with spaces in their
# names); not inside the namespace above, whose PATH has Ubuntu's compiler
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../.." && pwd)
WORK=${1:?"usage: build-bios.sh WORKDIR"}
TOOLCHAIN=${TOOLCHAIN:-ubuntu}

SEABIOS_GIT=https://github.com/coreboot/seabios.git
SEABIOS_COMMIT=81ec9ec0bcf45df11fb7f98339ec9036b546fca0
PATCHES="$TREE/seabios/patch $HERE/seabios-korvo.patch"
CONFIG=$TREE/seabios/config
NAME=seabios-korvo
export SB_STAMP_BIOS=${SB_STAMP_BIOS:-20261111_000000} SB_STAMP_VGA=${SB_STAMP_VGA:-20261019_000000}
export SB_HOST=korvo
for p in $PATCHES $CONFIG; do
    [ -f "$p" ] || { echo "$p is not there"; exit 1; }
done

# Ubuntu 26.04 (resolute), main, amd64: the packages of the compiler and what
# it needs to run, as pool paths with their sha256 from the archive's index
UBUNTU_MIRRORS="https://archive.ubuntu.com/ubuntu https://old-releases.ubuntu.com/ubuntu"
UBUNTU_DEBS="
g/gcc-15/gcc-15_15.2.0-16ubuntu1_amd64.deb 28da225c111d3ce9d230209cca24db46ec645f631f593f188bef718d8a1e4711
g/gcc-15/gcc-15-x86-64-linux-gnu_15.2.0-16ubuntu1_amd64.deb df0e5375c9e981b18e396c2e9ec1e359f226576a9b94a311bf07603f3ca6023a
g/gcc-15/cpp-15_15.2.0-16ubuntu1_amd64.deb baacc9086e17c8cadc3603365e660f907bca483de4446e56e6f1bc25da4c02be
g/gcc-15/cpp-15-x86-64-linux-gnu_15.2.0-16ubuntu1_amd64.deb 1649aa71c45b047feda5b9ca303c94799bdba5befa5c9de9735cbfc59c3ef93f
g/gcc-15/gcc-15-base_15.2.0-16ubuntu1_amd64.deb 5a302d705d562fb7e150415b00651c00fed58b633faadff0b332eac1b73fadb3
g/gcc-15/libgcc-15-dev_15.2.0-16ubuntu1_amd64.deb 32fa5bde1891f46fd4f422e466c863fb78e834b634e1aa08211c8eea68870af5
b/binutils/binutils_2.46-3ubuntu2_amd64.deb d14fc7bf644b495083f77c99ff168f1a2215c8b3b7ad5202e0345dc1092002b0
b/binutils/binutils-x86-64-linux-gnu_2.46-3ubuntu2_amd64.deb 91fa094c31783f7ccf0f0df8ed06447655c40476ba61fc95dbd245a313f8171e
b/binutils/libbinutils_2.46-3ubuntu2_amd64.deb 1376a5e49c8dc2ba938fadc15fce40dd01c7fbf1adf2d506a2a7cf7148e347c8
b/binutils/libctf0_2.46-3ubuntu2_amd64.deb 34b1757ce619720541c7e1e8d8f3aa506cb327731374e284ffb6ec872a2c566f
b/binutils/libctf-nobfd0_2.46-3ubuntu2_amd64.deb 5ca038ae110ddf65e190b9fe88822592abd45cecba5001f3e4ce7692ca1f31be
b/binutils/libsframe3_2.46-3ubuntu2_amd64.deb 05541ea1edb19e54bea297a21a9c33d0b99ca2cfe2e86d4a67335e6640667cfc
j/jansson/libjansson4_2.14-2build4_amd64.deb 1dc986ac3cf112384919d1a339a952e8a86bede9890a4489c016fdc266a036ca
g/glibc/libc6_2.43-2ubuntu2_amd64.deb c13775dc0c984403f3fcad229d14507a9f387763bd07ace1e5f93897ee6b8434
g/gcc-16/libgcc-s1_16-20260322-1ubuntu1_amd64.deb 2fb4d81c14fdf34251639ae82f5181f9f98480ea16125d535571ac1be9db3065
g/gcc-16/libstdc++6_16-20260322-1ubuntu1_amd64.deb a32b9ad585e39bdc7bd15d1eae6293461e6d466859a1dd2f7862d1e40f89b9d8
i/isl/libisl23_0.27-1build1_amd64.deb 02ea5de2ab6ddba978606ad58647bf6371ea68c0ed63aa5d6d0d3b8c1fe77ea3
m/mpclib3/libmpc3_1.3.1-3_amd64.deb a95c75b486f26fe8b140a632fd9e2883b7e2efc21f7586e155d27d2d2ff0ddc0
m/mpfr4/libmpfr6_4.2.2-3_amd64.deb 74defa71b7f81d66b103afccfce750ad356976696b40c96c846ddc956fcfe937
g/gmp/libgmp10_6.3.0+dfsg-5ubuntu2_amd64.deb a9bbe9d4a4bcd5875bbd0f53e67c379bc881d1c1a80522d51dfb4b107cc31819
libz/libzstd/libzstd1_1.5.7+dfsg-3_amd64.deb 34365d611ccf1f717f90a92fbe52ed96ae1829ab9be363c68a83563322797f8f
z/zlib/zlib1g_1.3.dfsg+really1.3.1-1ubuntu3_amd64.deb c45bbbf9c87457d90b8ba38720c5f01b9388c380d40f303c26f5c4953932da26
"

mkdir -p "$WORK"
cd "$WORK"

if [ "$TOOLCHAIN" = ubuntu ]; then
    echo "== Ubuntu 26.04's gcc 15.2.0 and binutils 2.46 =="
    U=$WORK/ubuntu
    mkdir -p "$U/debs"
    rm -rf "$U/root" "$U/bin"
    mkdir -p "$U/root" "$U/bin"
    echo "$UBUNTU_DEBS" | while read -r path sha; do
        [ -n "$path" ] || continue
        deb=$U/debs/${path##*/}
        if [ ! -f "$deb" ]; then
            for m in $UBUNTU_MIRRORS; do
                curl -fsSL -o "$deb" "$m/pool/main/$path" && break
                rm -f "$deb"
            done
        fi
        echo "$sha  $deb" | sha256sum -c --quiet -
        dpkg-deb -x "$deb" "$U/root"
    done
    # every program the build runs: Ubuntu's loader and libraries, not the
    # system's
    R=$U/root/usr
    for f in bin/x86_64-linux-gnu-gcc-15 bin/x86_64-linux-gnu-as bin/x86_64-linux-gnu-ld.bfd \
             bin/x86_64-linux-gnu-objcopy bin/x86_64-linux-gnu-objdump bin/x86_64-linux-gnu-strip \
             libexec/gcc/x86_64-linux-gnu/15/cc1 libexec/gcc/x86_64-linux-gnu/15/collect2 \
             libexec/gcc/x86_64-linux-gnu/15/lto-wrapper; do
        patchelf --set-interpreter "$R/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
                 --force-rpath --set-rpath "$R/lib/x86_64-linux-gnu" "$R/$f"
    done
    # SeaBIOS's Makefile calls them by these names (CC is make's cc)
    ln -s "$R/bin/x86_64-linux-gnu-gcc-15" "$U/bin/cc"
    ln -s "$R/bin/x86_64-linux-gnu-gcc-15" "$U/bin/gcc"
    for t in as objcopy objdump strip; do
        ln -s "$R/bin/x86_64-linux-gnu-$t" "$U/bin/$t"
    done
    ln -s "$R/bin/x86_64-linux-gnu-ld.bfd" "$U/bin/ld"
    RUN="env PATH=$U/bin:$PATH unshare -Urm bash $SELF --inside $U/root"
elif [ "$TOOLCHAIN" = host ]; then
    RUN=
else
    echo "TOOLCHAIN is ubuntu or host"; exit 1
fi

# python3 for SeaBIOS's make, with buildversion.py given the fixed time and
# host name
mkdir -p "$WORK/py"
cat > "$WORK/py/fixver.py" <<'EOF'
# Runs SeaBIOS's scripts/buildversion.py with a fixed build time and host name.
#   python3 fixver.py ./scripts/buildversion.py ARGS... OUT.h
# SB_STAMP_BIOS and SB_STAMP_VGA: the time (YYYYMMDD_HHMMSS) for
# out/autoversion.h and out/autovgaversion.h; SB_HOST: the host name.
import os, sys, time, socket, runpy
out = sys.argv[-1]
stamp = os.environ["SB_STAMP_VGA" if "vga" in os.path.basename(out) else "SB_STAMP_BIOS"]
time.strftime = lambda fmt, *a: stamp
socket.gethostname = lambda: os.environ["SB_HOST"]
sys.argv = sys.argv[1:]
runpy.run_path(sys.argv[0], run_name="__main__")
EOF
cat > "$WORK/py/python" <<'EOF'
#!/bin/sh
case "$1" in
    *buildversion.py) exec python3 "$(dirname "$0")/fixver.py" "$@" ;;
    *) exec python3 "$@" ;;
esac
EOF
chmod +x "$WORK/py/python"

echo "== SeaBIOS $SEABIOS_COMMIT =="
rm -rf seabios out
mkdir out
git clone -q $SEABIOS_GIT seabios
cd seabios
git checkout -q $SEABIOS_COMMIT
for p in $PATCHES; do
    echo "patch: ${p#$TREE/}"
    patch -p1 -s --no-backup-if-mismatch < "$p"
done
cp "$CONFIG" .config

# the source as it is built, with the version that git would give in .version
git describe --always --tags --long --dirty > .version
tar -C .. --exclude=.git --sort=name --mtime=@"$(git log -1 --format=%ct)" \
    --owner=0 --group=0 --numeric-owner --transform "s,^seabios,$NAME," \
    -cf - seabios | xz -9 > "../out/$NAME-src.tar.xz"
rm .version

echo "== build ($TOOLCHAIN) =="
$RUN sh -c "make oldconfig < /dev/null > ../oldconfig.log 2>&1 &&
            make PYTHON=$WORK/py/python > ../make.log 2>&1" ||
    { tail -20 ../make.log; exit 1; }
cmp -s .config "$CONFIG" ||
    { echo "make oldconfig changed .config: not the SeaBIOS this was made for"; exit 1; }
cp out/bios.bin out/vgabios.bin ../out/

echo "== done =="
cd ../out
ls -l bios.bin vgabios.bin $NAME-src.tar.xz
sha256sum bios.bin vgabios.bin
strings -n 8 bios.bin | grep -m1 "^rel-" | sed 's/^/version: /'
strings -n 8 bios.bin | grep -m1 "^gcc: "
