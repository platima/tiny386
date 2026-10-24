# v4.4: the boot code of the master boot record that sd_part.c makes for a
# card partition given to the guest whole (hda = sd:3).  The BIOS loads the
# record at 0000:7C00 and jumps to it with DL the boot drive.  This code
# moves itself to 0000:0600, finds the active entry of the table (the only
# one sd_part.c writes), reads that partition's first sector to 0000:7C00,
# through the BIOS's LBA calls (INT 13h AH=42h) when it has them, else by
# the entry's CHS bytes, and jumps to it as a DOS or Windows MBR does: DL
# the drive, DS:SI and DS:BP the entry.  On a failure it says so and gives
# the BIOS back the boot (INT 18h).
#
# Not built by the firmware: sd_part_mbr.h holds its bytes, and
# tests/host/sdpart/run_sdpart.sh assembles this file with GNU as and
# checks that they are the same (REGEN=1 writes sd_part_mbr.h again):
#   as --32 -o mbr.o sd_part_mbr.s
#   ld -m elf_i386 -Ttext=0x600 --oformat=binary -e start -o mbr.bin mbr.o
# At most 440 bytes: the disk signature follows at byte 440.

	.code16
	.text
	.globl	start
start:
	cli
	xorw	%ax, %ax
	movw	%ax, %ss
	movw	$0x7c00, %sp
	movw	%ax, %ds
	movw	%ax, %es
	sti
	cld
	movw	$0x7c00, %si
	movw	$0x0600, %di
	movw	$256, %cx
	rep	movsw
	ljmp	$0, $moved

moved:
	testb	$0x80, %dl		# a hard disk, as the BIOS gives it
	jnz	1f
	movb	$0x80, %dl
1:	movb	%dl, drive
	movw	$0x07be, %si		# the table: 0600h + 446
	movw	$4, %cx
find:	cmpb	$0x80, (%si)
	je	found
	addw	$16, %si
	loop	find
	movw	$msg_none, %si
	jmp	fail

found:	movw	%si, entry
	movb	$0x41, %ah		# LBA calls there?
	movw	$0x55aa, %bx
	movb	drive, %dl
	int	$0x13
	jc	chs
	cmpw	$0xaa55, %bx
	jne	chs
	testb	$1, %cl
	jz	chs
	movw	entry, %si
	movl	8(%si), %eax		# the partition's first sector
	movl	%eax, dap_lba
	movw	$dap, %si
	movb	$0x42, %ah
	movb	drive, %dl
	int	$0x13
	jnc	loaded

chs:	movw	entry, %si
	movw	$0x0201, %ax		# read one sector
	movw	$0x7c00, %bx
	movb	1(%si), %dh		# head
	movw	2(%si), %cx		# sector and cylinder
	movb	drive, %dl
	int	$0x13
	jc	ioerr

loaded:	cmpw	$0xaa55, 0x7dfe
	jne	nosys
	movw	entry, %si
	movw	%si, %bp
	movb	drive, %dl
	ljmp	$0, $0x7c00

ioerr:	movw	$msg_io, %si
	jmp	fail
nosys:	movw	$msg_nosys, %si
fail:	lodsb
	testb	%al, %al
	jz	give_up
	movb	$0x0e, %ah
	movw	$7, %bx
	int	$0x10
	jmp	fail
give_up:
	int	$0x18
1:	hlt
	jmp	1b

dap:	.byte	16, 0
	.word	1			# one sector
	.word	0x7c00, 0		# to 0000:7C00
dap_lba:
	.long	0, 0
drive:	.byte	0x80
entry:	.word	0

msg_none:
	.asciz	"tiny386: no active partition in the table\r\n"
msg_io:	.asciz	"tiny386: the partition's first sector could not be read\r\n"
msg_nosys:
	.asciz	"tiny386: the partition holds no boot sector\r\n"
