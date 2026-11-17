/*
 * The ESP-Mosaico's panel beyond lcd.h (lcd_mosaico.c): which part of the
 * 640x480 frame it shows.
 */
#ifndef LCD_MOSAICO_H
#define LCD_MOSAICO_H

#include <stdbool.h>

/* How a picture wider than the panel is shown: the whole of it at three
   quarters (480x360, rows 60 to 419), or 480 of its 640 columns 1:1 in a
   window that keeps the mouse pointer in it.  A picture 480 wide or less
   (Windows at 480x480) is always shown 1:1, as are the console's lines
   while the console has the panel: those in FIT. */
enum lcd_view { LCD_VIEW_FIT = 0, LCD_VIEW_FOLLOW = 1 };

void lcd_mosaico_set_view(enum lcd_view v);
enum lcd_view lcd_mosaico_view(void);
/* FIT to FOLLOW and back (Ctrl+Alt+F7, a short press of the AI button) */
void lcd_mosaico_next_view(void);

/* The guest's mouse moved (kbd_input.c, BOARD_MOUSE_HOOK): the window
   follows an estimate of where the pointer is. */
void lcd_mosaico_mouse(int dx, int dy);

/* The panel's clock in MHz, 10 to 80 ([esp] lcd_mhz): 0, or -1 if out of
   range or the bus would not take it (the clock stays as it was).  The SPI
   master divides 480 MHz, so it runs at the nearest clock it can make:
   40, 48, 53.3, 60, 68.6 and 80 MHz are exact; 70 is 80, for one.
   lcd_mosaico_mhz() is the clock asked for, lcd_mosaico_khz() the one the
   bus runs at.  A change sends the whole picture again. */
int lcd_mosaico_set_mhz(int mhz);
int lcd_mosaico_mhz(void);
int lcd_mosaico_khz(void);
/* Ctrl+Alt+F5: the next of 40, 48, 53, 60, 68 and 80 MHz, round again
   after 80, and a line on the console (Ctrl+Alt+F12) to say which */
void lcd_mosaico_next_mhz(void);

/* The drive strength of the panel's clock and data lines, 0 (weakest) to 3
   ([esp] lcd_drive; 2, the BSP's, unless it says otherwise): 0, or -1 if
   out of range.  A change sends the whole picture again. */
int lcd_mosaico_set_drive(int level);
int lcd_mosaico_drive(void);
/* Ctrl+Alt+F4: the next strength, 0 to 3 and round again, and a line on
   the console */
void lcd_mosaico_next_drive(void);

/* The panel's brightness, 0 to 255 (the CO5300's own, 51h) */
void lcd_mosaico_brightness(int level);

#endif /* LCD_MOSAICO_H */
