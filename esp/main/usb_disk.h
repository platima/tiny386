/*
 * USB disk mode on the ESP-Mosaico (usb_disk.c): the Type-C port as a USB
 * drive for a PC, with no PC emulated meanwhile.
 */
#ifndef USB_DISK_H
#define USB_DISK_H

#include <stdbool.h>

/* Whether to go into USB disk mode rather than start the PC: the AI button
   held while switching on, a restart into it that USB disk mode asked for,
   or no ini on the flash's FAT volume yet.  Called once the console is up
   (mosaico.c); mounts and lets go of /spiflash to look. */
bool usb_disk_wanted(void);
/* USB disk mode itself; it ends by restarting the board. */
void usb_disk_mode(void) __attribute__((noreturn));

#endif /* USB_DISK_H */
