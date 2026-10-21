/*
 * MacroSilicon MS7210 HDMI transmitter: RGB 4:4:4 in at 800x600 60 Hz,
 * HDMI (or DVI) out, I2S audio in at 48 kHz, 16 bits, two channels.
 *
 * Written for tiny386's DOS board from the register facts collected in
 * tiny386-hdmi-board/research/encoder.md section 8 (the table in 8.4, the
 * InfoFrames in 8.5, the run-time duties in 8.6).  No vendor code was used.
 *
 * The driver itself is plain C with no ESP-IDF in it, so that it builds and
 * runs on a PC against a fake I2C bus (tests/host/dosboard/ms7210_test.c).  The board
 * supplies the bus, the reset pin and a clock through ms7210_io_t.
 */
#ifndef MS7210_H
#define MS7210_H

#include <stdint.h>

#define MS7210_I2C_ADDR   0x59   /* 7-bit, SA pin high */
#define MS7210_EDID_ADDR  0x50   /* the monitor's EDID, through the DDC pass-through */
#define MS7210_PLL_TRIES  8      /* passes through E7 to E40 before giving up */

typedef struct ms7210_io {
	void *ctx;
	/* Raw I2C, 7-bit address.  Return 0 when every byte was acknowledged. */
	int (*i2c_write)(void *ctx, uint8_t addr7, const uint8_t *buf, int len);
	int (*i2c_write_read)(void *ctx, uint8_t addr7, const uint8_t *wbuf, int wlen,
			      uint8_t *rbuf, int rlen);
	/* SCL frequency for the transactions that follow. */
	void (*i2c_speed)(void *ctx, uint32_t hz);
	/* RSTN pin: 0 = reset. */
	void (*rstn)(void *ctx, int level);
	void (*delay_us)(void *ctx, uint32_t us);
	/* One line of log, no newline. */
	void (*log)(void *ctx, const char *line);
	/* Called with each row's name ("B22", "E40") before the row runs; may be NULL. */
	void (*row)(void *ctx, const char *id);
} ms7210_io_t;

enum {
	MS7210_MODE_AUTO = 0,    /* HDMI unless the EDID has no HDMI vendor block */
	MS7210_MODE_HDMI = 1,
	MS7210_MODE_DVI  = 2,
};

typedef struct {
	uint8_t pclk_phase;      /* register 0x1281 bits 2:0 (row B22): 0 = plan A, 4 = PCLK inverted */
	uint8_t mode;            /* MS7210_MODE_* */
	uint8_t audio;           /* 1 = audio enabled and its InfoFrame sent in HDMI mode */
	uint8_t rb_swap;         /* 1 = register 0x1201 bit 4 set (row B7): red and blue swapped */
	uint32_t bus_hz;         /* register access: 100 kHz */
	uint32_t ddc_hz;         /* while the DDC pass-through is on: 20 kHz */
} ms7210_cfg_t;

typedef struct {
	int valid;               /* block 0 header and checksum right */
	int blocks;              /* blocks read and checked (1 or 2) */
	int ext_ok;              /* block 1 is a CEA-861 extension with a good checksum */
	int hdmi;                /* CEA-861 vendor block with IEEE OUI 00-0C-03 */
	int basic_audio;         /* CEA-861 header byte 3 bit 6 */
	int est_800x600_60;      /* established timings I, byte 0x23 bit 0 */
	int std_800x600_60;      /* a standard timing entry 0x45 0x40 */
	int dtd_800x600;         /* a detailed timing descriptor of 800x600 */
	char vendor[4];          /* three letters from bytes 8 and 9 */
	uint16_t product;
	char name[14];           /* monitor name descriptor (0xFC) */
} ms7210_edid_info_t;

typedef struct {
	const ms7210_io_t *io;
	ms7210_cfg_t cfg;
	uint8_t chip_id[3];
	int found;               /* chip ID read back as A2 20 0A */
	uint16_t pclk_meter;     /* row C1: about 6068 at 40 MHz */
	uint16_t pclk_meter_late; /* the same, read again after section E */
	int pll_tries;           /* passes through E7 to E40 in the last output set-up */
	int pll_locked;
	int hpd;                 /* last hot plug state seen */
	int dvi;                 /* output mode in use: 1 = DVI */
	int input_stable;        /* 0x051D bit 4 after the last set-up */
	int edid_ok;
	uint8_t edid[256];
	ms7210_edid_info_t edid_info;
	int errors;              /* I2C errors since the last ms7210_init() */
} ms7210_t;

void ms7210_default_cfg(ms7210_cfg_t *cfg);

/* Reset pulse and chip ID (rows A1 to A4).  0 when the chip answers A2 20 0A. */
int ms7210_probe(ms7210_t *m, const ms7210_io_t *io, const ms7210_cfg_t *cfg);

/* Input, timing and output set-up (rows B, C, D, E).  Needs PCLK running.
   Reads the EDID first when a monitor is attached.  0 = done without I2C errors. */
int ms7210_init(ms7210_t *m);

/* Hot plug, about twice a second: on a new connection read the EDID and set
   up the output again (section E).  1 = set up again, 0 = nothing to do, -1 = I2C error,
   2 = the chip had lost its settings (it was reset) and was set up again from section B. */
int ms7210_poll(ms7210_t *m);

/* EDID through the DDC pass-through into m->edid, parsed into m->edid_info.  0 = valid. */
int ms7210_read_edid(ms7210_t *m);
int ms7210_edid_parse(const uint8_t *e, int len, ms7210_edid_info_t *out);

/* InfoFrame payloads as the MS7210 takes them at 0x050D: checksum, then the
   data bytes.  The header (type, version, length) goes to 0x050A, 0x0509, 0x050B. */
#define MS7210_AVI_TYPE   0x82
#define MS7210_AVI_VER    0x02
#define MS7210_AVI_LEN    13
#define MS7210_AIF_TYPE   0x84
#define MS7210_AIF_VER    0x01
#define MS7210_AIF_LEN    10
void ms7210_avi_infoframe(uint8_t out[1 + MS7210_AVI_LEN]);
void ms7210_audio_infoframe(uint8_t out[1 + MS7210_AIF_LEN]);

/* Register access: 16-bit index, low byte first on the wire. */
int ms7210_write(ms7210_t *m, uint16_t reg, uint8_t val);
int ms7210_read(ms7210_t *m, uint16_t reg, uint8_t *val);

/* The sequence, for tests and logs. */
typedef struct {
	const char *id;          /* row name in encoder.md 8.4 */
	uint8_t op;
	uint8_t flags;
	uint16_t reg;
	uint8_t mask;
	uint8_t val;
	uint16_t arg;
} ms7210_row_t;
extern const ms7210_row_t ms7210_rows[];

enum {
	MS7210_OP_END = 0,
	MS7210_OP_RSTN,          /* low for arg ms, high, then wait val ms */
	MS7210_OP_EXPECT,        /* read reg, expect val */
	MS7210_OP_WRITE,         /* reg = val */
	MS7210_OP_RMW,           /* reg = (reg & ~mask) | val */
	MS7210_OP_WRITE16,       /* reg = arg & 0xFF, reg + 1 = arg >> 8 */
	MS7210_OP_WAIT_MS,       /* arg ms */
	MS7210_OP_WAIT_US,       /* arg us */
	MS7210_OP_METER,         /* read reg, reg + 1 into pclk_meter */
	MS7210_OP_AVI,           /* AVI InfoFrame payload to reg, one byte at a time */
	MS7210_OP_AIF,           /* audio InfoFrame payload to reg */
};

/* Row flags */
#define MS7210_F_PHASE   0x01    /* value is cfg.pclk_phase */
#define MS7210_F_DVI     0x02    /* value is 0x01 in DVI mode, 0x00 in HDMI mode */
#define MS7210_F_AUDIO   0x04    /* value only in HDMI mode with audio on, else 0x00 */
#define MS7210_F_HDMI    0x08    /* row left out in DVI mode */
#define MS7210_F_PLL     0x10    /* after the row: check PLL lock, maybe go back to E7 */
#define MS7210_F_RBSWAP  0x20    /* value is the row's mask if cfg.rb_swap, else 0x00 */

#endif /* MS7210_H */
