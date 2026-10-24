#!/bin/sh
# export-root.sh: run on the board's Linux, with the OLD card in.  Copies
# Linux's root (the card's p2) and the card's first sector onto the card's
# WIN9X partition (p3), for newcard.sh on the PC to put on the new card:
#
#   korvo-root.img      p2, every sector
#   korvo-mbr.bin       sector 0 (newcard.sh keeps its disk signature)
#   korvo-export.txt    what was copied, and the SHA-256 of both files
#
#   sh export-root.sh [FOLDER]
#
# FOLDER (default /mnt/win) must be on a FAT partition of the card with
# room for p2; if /mnt/win is not mounted, p3 is mounted there first (and
# unmounted again at the end).  While p2 is copied it is made read-only
# (mount -o remount,ro), so that nothing changes it part way through, and
# read-write again after; if it cannot be made read-only the script stops
# without copying.  It writes nothing on p2 and nothing outside FOLDER.
# POSIX sh and BusyBox's dd, mount, df and sha256sum.
set -eu
DEV=${EXPORT_DEV:-/dev/mmcblk0}         # EXPORT_DEV: for testing on a PC only
P2=${DEV}p2
DST=${1:-/mnt/win}

say() { printf '%s\n' "$*"; }
die() { say "export-root.sh: $*"; exit 1; }

[ -e "$P2" ] || die "there is no $P2: is this the board's Linux, with the card in?"
# p2's size in 512-byte sectors, from sysfs (a file's size when testing)
if [ -b "$P2" ]; then
    n=$(cat "/sys/class/block/$(basename "$P2")/size")
else
    n=$(( $(wc -c < "$P2") / 512 ))
fi
[ "$n" -gt 0 ] || die "$P2 has no size"

mounted_here=
if ! grep -q " $DST " /proc/mounts 2>/dev/null && [ -z "${EXPORT_DEV:-}" ]; then
    [ "$DST" = /mnt/win ] || die "$DST is not mounted"
    [ -b "${DEV}p3" ] || die "$DST is not mounted, and there is no ${DEV}p3 to mount there"
    mkdir -p /mnt/win
    mount -t vfat "${DEV}p3" /mnt/win || die "${DEV}p3 would not mount at /mnt/win"
    mounted_here=1
    say "mounted ${DEV}p3 at /mnt/win"
fi
[ -d "$DST" ] || die "there is no folder $DST"
free=$(df -k "$DST" | awk 'NR == 2 { print $4 }')
[ -n "$free" ] || die "df cannot say how much room $DST has"
need=$(( n / 2 + 1024 ))
[ "$free" -ge "$need" ] || die "$DST has $free KiB free; p2 needs $need KiB"

# where p2 is mounted, if it is (the root, on Linux with its root on the card)
mp=$(awk -v d="$P2" '$1 == d { print $2; exit }' /proc/mounts 2>/dev/null || true)
# a root the kernel mounted itself (root=/dev/mmcblk0p2) shows as /dev/root
if [ -z "$mp" ] && [ -z "${EXPORT_DEV:-}" ]; then
    case " $(cat /proc/cmdline 2>/dev/null) " in
    *" root=$P2 "*)
        if awk '$1 == "/dev/root" && $2 == "/" { f = 1 } END { exit !f }' /proc/mounts; then
            mp=/
        fi ;;
    esac
fi
ro=
if [ -n "$mp" ] && [ -z "${EXPORT_DEV:-}" ]; then
    sync
    mount -o remount,ro "$mp" || die "$P2 (at $mp) could not be made read-only, so it is not copied: \
stop what writes to it, or copy it another way (newcard/README.md)"
    ro=1
    say "$P2 (at $mp) is read-only while it is copied"
fi
say "copying $P2, $n sectors ($(( n / 2048 )) MiB), to $DST/korvo-root.img ..."
rc=0
dd if="$P2" of="$DST/korvo-root.img" bs=1M 2>/dev/null || rc=$?
if [ -n "$ro" ]; then
    mount -o remount,rw "$mp" || say "WARNING: $mp is still read-only: mount -o remount,rw $mp, or restart"
fi
[ "$rc" = 0 ] || die "dd stopped (exit $rc); $DST/korvo-root.img is not whole"
dd if="$DEV" of="$DST/korvo-mbr.bin" bs=512 count=1 2>/dev/null
sync
got=$(( $(wc -c < "$DST/korvo-root.img") / 512 ))
[ "$got" = "$n" ] || die "$DST/korvo-root.img is $got sectors, not $n"
{
    say "export-root.sh on $(uname -n), $(date 2>/dev/null)"
    say "$P2: $n sectors$( [ -n "$mp" ] && printf ', mounted at %s, read-only while copied' "$mp")"
    (cd "$DST" && sha256sum korvo-root.img korvo-mbr.bin)
} > "$DST/korvo-export.txt"
sync
cat "$DST/korvo-export.txt"
if [ -n "$mounted_here" ]; then
    umount /mnt/win && say "unmounted /mnt/win"
fi
say "done: shut down (poweroff), then copy korvo-root.img and korvo-mbr.bin off the card's WIN9X drive on the PC"
