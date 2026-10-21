#ifndef I8042_H
#define I8042_H

typedef struct PS2MouseState PS2MouseState;
typedef struct PS2KbdState PS2KbdState;

PS2KbdState *ps2_kbd_init(void (*update_irq)(void *, int), void *update_arg);
PS2MouseState *ps2_mouse_init(void (*update_irq)(void *, int), void *update_arg);
void ps2_write_mouse(void *, int val);
void ps2_write_keyboard(void *, int val);
uint32_t ps2_read_data(void *);
void ps2_queue(void *, int b);
void ps2_keyboard_set_translation(void *opaque, int mode);

void ps2_put_keycode(PS2KbdState *s, int is_down, int keycode);
/* With PS2_KBD_HOOKS this may be called from another thread than the
   emulator's (one caller at a time): the report waits in a ring and
   kbd_step() makes the packets. */
void ps2_mouse_event(PS2MouseState *s,
                     int dx, int dy, int dz, int buttons_state);

#ifdef PS2_KBD_HOOKS
/* What the guest tells the keyboard, handed to the front end (the ESP-IDF
   port's kbd_input.c), which defines these two: the lights of command 0xED
   (bit 0 Scroll, 1 Num, 2 Caps Lock) and the typematic byte of 0xF3 (also
   given 0x2B, the default, at a reset).  Called from the emulator's own
   thread. */
void ps2_kbd_leds_changed(int ps2_leds);
void ps2_kbd_rate_changed(int rate);
/* bytes waiting for the guest to read */
int ps2_kbd_pending(PS2KbdState *s);
#endif

typedef struct KBDState KBDState;

uint32_t kbd_read_status(void *opaque, uint32_t addr);
void kbd_write_command(void *opaque, uint32_t addr, uint32_t val);
uint32_t kbd_read_data(void *opaque, uint32_t addr);
void kbd_write_data(void *opaque, uint32_t addr, uint32_t val);
void kbd_step(void *opaque);

KBDState *i8042_init(PS2KbdState **pkbd,
                     PS2MouseState **pmouse,
                     int kbd_irq, int mouse_irq,
		     void *pic,
                     void (*set_irq)(void *pic, int irq, int level),
                     void *sys,
                     void (*system_reset_request)(void *sys));

#endif /* I8042_H */
