/*
 * MacroSilicon MS7210 HDMI transmitter, clean-room driver for the DOS board.
 *
 * Every register, mask and value below comes from the table in
 * tiny386-hdmi-board/research/encoder.md section 8.4, row for row, and the
 * row names are kept so that a log or a test can be read against it.  The
 * run-time duties (PLL lock retry, hot plug, EDID, DVI mode) follow its
 * section 8.6, the InfoFrames its section 8.5.  No vendor code was copied
 * or consulted for this file.
 *
 * Plain C: the board's bus, pins and clock come in through ms7210_io_t.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ms7210.h"

#define R(id, op, fl, reg, mask, val, arg) \
	{ id, MS7210_OP_##op, fl, reg, mask, val, arg }
#define PHASE MS7210_F_PHASE
#define DVI   MS7210_F_DVI
#define AUD   MS7210_F_AUDIO
#define HDMI  MS7210_F_HDMI
#define PLL   MS7210_F_PLL
#define RBSW  MS7210_F_RBSWAP

const ms7210_row_t ms7210_rows[] = {
	/* A. Reset and identity */
	R("A1",  RSTN,    0, 0,      0,    50,   20),   /* low 20 ms (>= 10), high, 50 ms */
	R("A2",  EXPECT,  0, 0x0000, 0,    0xA2, 0),    /* chip ID 0 */
	R("A3",  EXPECT,  0, 0x0001, 0,    0x20, 0),    /* chip ID 1 */
	R("A4",  EXPECT,  0, 0x0002, 0,    0x0A, 0),    /* chip ID 2 */

	/* B. Video and audio input set-up */
	R("B1",  RMW,     0, 0x0009, 0x01, 0x00, 0),    /* hold DVIN in reset */
	R("B2",  RMW,     0, 0x0016, 0x04, 0x00, 0),    /* DVIN clock off */
	R("B3",  WRITE,   0, 0x0060, 0,    0x81, 0),    /* RC oscillator trim */
	R("B4",  WRITE,   0, 0x0061, 0,    0x54, 0),    /* RC oscillator trim */
	R("B5",  RMW,     0, 0x0080, 0x03, 0x00, 0),    /* CSC input RGB */
	R("B6",  RMW,     0, 0x1202, 0x07, 0x00, 0),    /* input mode RGB/YUV444 */
	R("B7",  RMW,  RBSW, 0x1201, 0x10, 0x00, 0),    /* no R/B swap ([dosboard] rb_swap) */
	R("B8",  RMW,     0, 0x1202, 0x08, 0x00, 0),    /* SDR */
	R("B9",  RMW,     0, 0x00C0, 0x03, 0x00, 0),    /* input clock ratio 1:1 */
	R("B10", RMW,     0, 0x1200, 0x0C, 0x04, 0),    /* separate HSYNC, VSYNC, DE */
	R("B11", RMW,     0, 0x1202, 0xE0, 0x00, 0),    /* no embedded sync */
	R("B12", RMW,     0, 0x1265, 0x01, 0x00, 0),    /* not DE-only */
	R("B13", RMW,     0, 0x1262, 0x01, 0x00, 0),    /* not BTA-T1004 */
	R("B14", RMW,     0, 0x120B, 0x01, 0x00, 0),    /* use the DE pin */
	R("B15", RMW,     0, 0x0079, 0x30, 0x20, 0),    /* pull-downs on digital input pads */
	R("B16", RMW,     0, 0x0008, 0x03, 0x00, 0),    /* audio clock from pad */
	R("B17", RMW,     0, 0x00A5, 0x18, 0x00, 0),    /* pin mux: I2S */
	R("B18", RMW,     0, 0x0530, 0x01, 0x00, 0),    /* audio data from I2S */
	R("B19", WRITE,   0, 0x0019, 0,    0x07, 0),    /* PCLK meter on */
	R("B20", RMW,     0, 0x0016, 0x04, 0x04, 0),    /* DVIN clock on */
	R("B21", RMW,     0, 0x0009, 0x01, 0x01, 0),    /* DVIN out of reset */
	R("B22", RMW, PHASE, 0x1281, 0x07, 0x00, 0),    /* input sampling phase */

	/* C. PCLK meter: f = word * 2700 / 4096 in 10 kHz units */
	R("C1",  METER,   0, 0x001E, 0,    0,    0),

	/* D. Input timing, VESA 800x600 60 Hz (htotal 1056, vtotal 628,
	   hoffset 128 + 88 = 216, voffset 4 + 23 = 27) */
	R("D1",  WRITE16, 0, 0x120C, 0,    0,    1056), /* H total */
	R("D2",  WRITE16, 0, 0x120E, 0,    0,    628),  /* V total */
	R("D3",  WRITE16, 0, 0x1210, 0,    0,    219),  /* pixel shift 3 + 216 */
	R("D4",  WRITE16, 0, 0x1212, 0,    0,    2),    /* line shift */
	R("D5",  WRITE16, 0, 0x1214, 0,    0,    128),  /* H sync width */
	R("D6",  WRITE16, 0, 0x1216, 0,    0,    4),    /* V sync width */
	R("D7",  WRITE16, 0, 0x1218, 0,    0,    216),  /* H DE start */
	R("D8",  WRITE16, 0, 0x121A, 0,    0,    1016), /* H DE stop, 216 + 800 */
	R("D9",  WRITE16, 0, 0x121C, 0,    0,    27),   /* V DE odd start */
	R("D10", WRITE16, 0, 0x121E, 0,    0,    627),  /* V DE odd stop, 27 + 600 */
	R("D11", WRITE16, 0, 0x1220, 0,    0,    342),  /* V DE even start, 27 + 314 + 1 */
	R("D12", WRITE16, 0, 0x1222, 0,    0,    642),  /* V DE even stop, 27 + 314 + 300 + 1 */
	R("D13", RMW,     0, 0x1206, 0x03, 0x00, 0),    /* sync inputs active high */
	R("D14", RMW,     0, 0x120B, 0x34, 0x04, 0),    /* output syncs positive, progressive */
	R("D15", RMW,     0, 0x092A, 0x44, 0x00, 0),    /* TMDS clock = pixel clock */

	/* E. Output, TMDS clock under 100 MHz */
	R("E1",  RMW,     0, 0x0920, 0x04, 0x00, 0),    /* TMDS clock driver off */
	R("E2",  RMW,     0, 0x091F, 0x07, 0x00, 0),    /* TMDS data drivers off */
	R("E3",  WRITE,   0, 0x0581, 0,    0x04, 0),    /* HDCP off, step 1 */
	R("E4",  WRITE,   0, 0x0581, 0,    0x00, 0),    /* HDCP off, step 2 */
	R("E5",  RMW,     0, 0x050E, 0x80, 0x00, 0),    /* GCP AVMUTE off */
	R("E6",  RMW,     0, 0x0080, 0x0C, 0x00, 0),    /* CSC output RGB */
	R("E7",  RMW,     0, 0x0007, 0x0D, 0x09, 0),    /* TMDS clock from DVIN */
	R("E8",  RMW,     0, 0x0901, 0x18, 0x00, 0),    /* PLL clock select */
	R("E9",  RMW,     0, 0x090F, 0x07, 0x00, 0),    /* PLL hardware auto mode */
	R("E10", RMW,     0, 0x0901, 0x01, 0x01, 0),    /* PLL control */
	R("E11", RMW,     0, 0x0904, 0x08, 0x08, 0),    /* PLL control */
	R("E12", RMW,     0, 0x0008, 0xF0, 0xF0, 0),    /* audio clock select */
	R("E13", RMW,     0, 0x000A, 0xF0, 0xF0, 0),    /* reset control */
	R("E14", RMW,     0, 0x0925, 0x70, 0x40, 0),    /* clock lane pre-drive 4 */
	R("E15", RMW,     0, 0x0927, 0xF0, 0x20, 0),    /* clock lane main drive 2 */
	R("E16", RMW,     0, 0x0924, 0x07, 0x04, 0),    /* lane 0 pre-drive 4 */
	R("E17", RMW,     0, 0x0926, 0x0F, 0x04, 0),    /* lane 0 main drive 4 */
	R("E18", RMW,     0, 0x0923, 0x01, 0x01, 0),    /* lane 0 post pre */
	R("E19", RMW,     0, 0x0928, 0x0F, 0x07, 0),    /* lane 0 post drive 7 */
	R("E20", RMW,     0, 0x0924, 0x70, 0x40, 0),    /* lane 1 pre-drive */
	R("E21", RMW,     0, 0x0926, 0xF0, 0x40, 0),    /* lane 1 main drive */
	R("E22", RMW,     0, 0x0923, 0x02, 0x02, 0),    /* lane 1 post pre */
	R("E23", RMW,     0, 0x0928, 0xF0, 0x70, 0),    /* lane 1 post drive */
	R("E24", RMW,     0, 0x0925, 0x07, 0x04, 0),    /* lane 2 pre-drive */
	R("E25", RMW,     0, 0x0927, 0x0F, 0x04, 0),    /* lane 2 main drive */
	R("E26", RMW,     0, 0x0923, 0x04, 0x04, 0),    /* lane 2 post pre */
	R("E27", RMW,     0, 0x0929, 0x0F, 0x07, 0),    /* lane 2 post drive */
	R("E28", WRITE,   0, 0x0900, 0,    0x20, 0),    /* PLL power */
	R("E29", RMW,     0, 0x0906, 0x04, 0x00, 0),    /* low-rate PLL band */
	R("E30", RMW,     0, 0x0920, 0x40, 0x00, 0),    /* low-rate PHY */
	R("E31", WRITE,   0, 0x0926, 0,    0x11, 0),    /* drive, low band */
	R("E32", WRITE,   0, 0x0927, 0,    0x01, 0),
	R("E33", WRITE,   0, 0x0928, 0,    0x11, 0),
	R("E34", WRITE,   0, 0x0929, 0,    0x01, 0),
	R("E35", RMW,     0, 0x0905, 0x03, 0x02, 0),    /* HBR audio clock divider */
	R("E36", RMW,     0, 0x0900, 0x07, 0x00, 0),    /* PLL power on */
	R("E37", RMW,     0, 0x0920, 0x2A, 0x0A, 0),    /* PHY power on */
	R("E38", WAIT_MS, 0, 0,      0,    0,    10),   /* PLL supply settles */
	R("E39", WRITE,   0, 0x0910, 0,    0x01, 0),    /* trigger PLL */
	R("E40", WAIT_MS, PLL, 0,    0,    0,    10),   /* PLL clock settles; lock check */
	R("E41", RMW,     0, 0x000B, 0x11, 0x00, 0),    /* HDMI shell into reset */
	R("E42", RMW,     0, 0x0570, 0x01, 0x01, 0),    /* S/PDIF format fix */
	R("E43", RMW,     0, 0x0006, 0x11, 0x11, 0),    /* video and audio clocks on */
	R("E44", RMW,     0, 0x0048, 0x0F, 0x00, 0),    /* audio control */
	R("E45", RMW,   AUD, 0x0531, 0x80, 0x80, 0),    /* audio enable */
	R("E46", RMW,   DVI, 0x0503, 0x01, 0x00, 0),    /* HDMI mode (0x01 = DVI) */
	R("E47", RMW,     0, 0x0508, 0x30, 0x00, 0),    /* no pixel repetition */
	R("E48", RMW,     0, 0x0508, 0xC0, 0x00, 0),    /* colour space RGB */
	R("E49", RMW,     0, 0x0508, 0x07, 0x00, 0),    /* 8 bits a colour */
	R("E50", RMW,     0, 0x0531, 0x7F, 0x04, 0),    /* audio rate 48 kHz */
	R("E51", RMW,     0, 0x0535, 0x30, 0x10, 0),    /* 16-bit samples */
	R("E52", RMW,     0, 0x0532, 0x0F, 0x01, 0),    /* 2 channels */
	R("E53", RMW,     0, 0x000B, 0x11, 0x11, 0),    /* shell out of reset */
	R("E54", RMW,     0, 0x050E, 0x40, 0x00, 0),    /* AVI InfoFrame off while loading */
	R("E55", WRITE,   0, 0x050A, 0,    MS7210_AVI_TYPE, 0),
	R("E56", WRITE,   0, 0x0509, 0,    MS7210_AVI_VER, 0),
	R("E57", WRITE,   0, 0x050B, 0,    MS7210_AVI_LEN, 0),
	R("E58", AVI,     0, 0x050D, 0,    0,    1 + MS7210_AVI_LEN),
	R("E59", RMW,  HDMI, 0x050E, 0x40, 0x40, 0),    /* AVI InfoFrame on */
	R("E60", RMW,     0, 0x050E, 0x20, 0x00, 0),    /* audio InfoFrame off while loading */
	R("E61", WRITE,   0, 0x050A, 0,    MS7210_AIF_TYPE, 0),
	R("E62", WRITE,   0, 0x0509, 0,    MS7210_AIF_VER, 0),
	R("E63", WRITE,   0, 0x050B, 0,    MS7210_AIF_LEN, 0),
	R("E64", AIF,     0, 0x050D, 0,    0,    1 + MS7210_AIF_LEN),
	R("E65", RMW, HDMI | AUD, 0x050E, 0x20, 0x20, 0), /* audio InfoFrame on */
	R("E66", RMW,     0, 0x0506, 0x06, 0x06, 0),    /* audio FIFO reset */
	R("E67", RMW,     0, 0x0506, 0x04, 0x00, 0),    /* release */
	R("E68", WAIT_US, 0, 0,      0,    0,    100),
	R("E69", RMW,     0, 0x0506, 0x02, 0x00, 0),    /* release */
	R("E70", RMW,     0, 0x0507, 0x06, 0x00, 0),    /* video and audio unmuted */
	R("E71", RMW,     0, 0x091F, 0x07, 0x07, 0),    /* TMDS data drivers on */
	R("E72", RMW,     0, 0x0920, 0x04, 0x04, 0),    /* TMDS clock driver on */
	{ NULL, MS7210_OP_END, 0, 0, 0, 0, 0 },
};

#undef R
#undef PHASE
#undef DVI
#undef AUD
#undef HDMI
#undef PLL
#undef RBSW

/* Status registers (encoder.md 8.6, 8.7) */
#define REG_HPD        0x0502   /* bit 0: hot plug */
#define REG_PLL_LOCK   0x091C   /* bit 7: PLL locked */
#define REG_IN_STABLE  0x051D   /* bit 4: input stable */
#define REG_DDC_SEL    0x0013   /* 0xFF before the pass-through is turned on */
#define REG_DDC_PASS   0x0012   /* 0x0E = pass-through on (output 0), 0x0F = off */

static void logf_(ms7210_t *m, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void logf_(ms7210_t *m, const char *fmt, ...)
{
	char line[160];
	va_list ap;

	if (!m->io->log)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	m->io->log(m->io->ctx, line);
}

void ms7210_default_cfg(ms7210_cfg_t *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->pclk_phase = 0;
	cfg->mode = MS7210_MODE_AUTO;
	cfg->audio = 1;
	cfg->bus_hz = 100000;
	cfg->ddc_hz = 20000;
}

int ms7210_write(ms7210_t *m, uint16_t reg, uint8_t val)
{
	uint8_t b[3] = { reg & 0xFF, reg >> 8, val };
	int r = m->io->i2c_write(m->io->ctx, MS7210_I2C_ADDR, b, 3);

	if (r)
		m->errors++;
	return r;
}

int ms7210_read(ms7210_t *m, uint16_t reg, uint8_t *val)
{
	uint8_t b[2] = { reg & 0xFF, reg >> 8 };
	int r = m->io->i2c_write_read(m->io->ctx, MS7210_I2C_ADDR, b, 2, val, 1);

	if (r) {
		m->errors++;
		*val = 0;
	}
	return r;
}

static int rmw(ms7210_t *m, uint16_t reg, uint8_t mask, uint8_t val)
{
	uint8_t v;

	if (ms7210_read(m, reg, &v))
		return -1;
	return ms7210_write(m, reg, (v & ~mask) | (val & mask));
}

static void delay_ms(ms7210_t *m, uint32_t ms)
{
	m->io->delay_us(m->io->ctx, ms * 1000);
}

static uint8_t sum8(const uint8_t *p, int n)
{
	uint8_t s = 0;

	while (n--)
		s += *p++;
	return s;
}

/* AVI InfoFrame (encoder.md 8.5): RGB, active format present, underscan
   (IT content), no colorimetry, 4:3, same as picture, default range, VIC 0
   (800x600 is not a CEA-861 format), no repetition, no bars. */
void ms7210_avi_infoframe(uint8_t out[1 + MS7210_AVI_LEN])
{
	const uint8_t hdr[3] = { MS7210_AVI_TYPE, MS7210_AVI_VER, MS7210_AVI_LEN };
	uint8_t *d = out + 1;

	memset(out, 0, 1 + MS7210_AVI_LEN);
	d[0] = (0 << 5)          /* Y: RGB */
	     | (1 << 4)          /* A0: active format information present */
	     | 2;                /* S: underscan */
	d[1] = (0 << 6)          /* C: no colorimetry data */
	     | (1 << 4)          /* M: 4:3 */
	     | 8;                /* R: same as the picture */
	/* d[2] default quantisation, d[3] VIC 0, d[4] no repetition, bars 0 */
	out[0] = (uint8_t)(0x100 - (uint8_t)(sum8(hdr, 3) + sum8(d, MS7210_AVI_LEN)));
}

/* Audio InfoFrame: coding type and rate from the stream, two channels. */
void ms7210_audio_infoframe(uint8_t out[1 + MS7210_AIF_LEN])
{
	const uint8_t hdr[3] = { MS7210_AIF_TYPE, MS7210_AIF_VER, MS7210_AIF_LEN };
	uint8_t *d = out + 1;

	memset(out, 0, 1 + MS7210_AIF_LEN);
	d[0] = (0 << 4)          /* CT: refer to the stream header */
	     | (2 - 1);          /* CC: channel count less one */
	out[0] = (uint8_t)(0x100 - (uint8_t)(sum8(hdr, 3) + sum8(d, MS7210_AIF_LEN)));
}

static int row_index(const char *id)
{
	for (int i = 0; ms7210_rows[i].id; i++)
		if (strcmp(ms7210_rows[i].id, id) == 0)
			return i;
	return -1;
}

static int check_pll(ms7210_t *m)
{
	uint8_t v;

	if (ms7210_read(m, REG_PLL_LOCK, &v))
		return 0;
	return (v & 0x80) != 0;
}

/* Run rows from..to (names), inclusive.  Stops early only on a wrong chip ID
   or when the chip stops answering. */
static int run(ms7210_t *m, const char *from, const char *to)
{
	int first = row_index(from), last = row_index(to);
	int e7 = row_index("E7");
	int start_errors = m->errors;

	if (first < 0 || last < first)
		return -1;
	for (int i = first; i <= last; i++) {
		const ms7210_row_t *r = &ms7210_rows[i];
		uint8_t val = r->val, v;
		uint8_t buf[1 + MS7210_AVI_LEN];

		if (r->flags & MS7210_F_PHASE)
			val = m->cfg.pclk_phase & 0x07;
		if (r->flags & MS7210_F_RBSWAP)
			val = m->cfg.rb_swap ? r->mask : 0x00;
		if (r->flags & MS7210_F_DVI)
			val = m->dvi ? 0x01 : 0x00;
		if ((r->flags & MS7210_F_AUDIO) && (m->dvi || !m->cfg.audio))
			val = 0x00;
		if ((r->flags & MS7210_F_HDMI) && m->dvi)
			continue;

		if (m->io->row)
			m->io->row(m->io->ctx, r->id);

		switch (r->op) {
		case MS7210_OP_RSTN:
			m->io->rstn(m->io->ctx, 0);
			delay_ms(m, r->arg);
			m->io->rstn(m->io->ctx, 1);
			delay_ms(m, r->val);
			break;
		case MS7210_OP_EXPECT:
			if (ms7210_read(m, r->reg, &v))
				return -1;
			if (r->reg <= 2)
				m->chip_id[r->reg] = v;
			if (v != val) {
				logf_(m, "ms7210: register 0x%04X reads 0x%02X, expected 0x%02X",
				      r->reg, v, val);
				return -1;
			}
			break;
		case MS7210_OP_WRITE:
			ms7210_write(m, r->reg, val);
			break;
		case MS7210_OP_RMW:
			rmw(m, r->reg, r->mask, val);
			break;
		case MS7210_OP_WRITE16:
			ms7210_write(m, r->reg, r->arg & 0xFF);
			ms7210_write(m, r->reg + 1, r->arg >> 8);
			break;
		case MS7210_OP_WAIT_MS:
			delay_ms(m, r->arg);
			break;
		case MS7210_OP_WAIT_US:
			m->io->delay_us(m->io->ctx, r->arg);
			break;
		case MS7210_OP_METER: {
			uint8_t lo, hi;
			ms7210_read(m, r->reg, &lo);
			ms7210_read(m, r->reg + 1, &hi);
			m->pclk_meter = lo | (hi << 8);
			break;
		}
		case MS7210_OP_AVI:
		case MS7210_OP_AIF:
			if (r->op == MS7210_OP_AVI)
				ms7210_avi_infoframe(buf);
			else
				ms7210_audio_infoframe(buf);
			for (int k = 0; k < r->arg; k++)
				ms7210_write(m, r->reg, buf[k]);
			break;
		}

		if (r->flags & MS7210_F_PLL) {
			m->pll_locked = check_pll(m);
			if (!m->pll_locked && m->pll_tries < MS7210_PLL_TRIES && e7 >= 0) {
				logf_(m, "ms7210: PLL not locked after pass %d, again from E7",
				      m->pll_tries);
				m->pll_tries++;
				i = e7 - 1;
				continue;
			}
		}
		/* a chip that has stopped answering: give up rather than wait
		   out every row */
		if (m->errors - start_errors > 8) {
			logf_(m, "ms7210: no answer on I2C, stopped at row %s", r->id);
			return -1;
		}
	}
	return m->errors == start_errors ? 0 : -1;
}

int ms7210_probe(ms7210_t *m, const ms7210_io_t *io, const ms7210_cfg_t *cfg)
{
	memset(m, 0, sizeof(*m));
	m->io = io;
	if (cfg)
		m->cfg = *cfg;
	else
		ms7210_default_cfg(&m->cfg);
	if (io->i2c_speed)
		io->i2c_speed(io->ctx, m->cfg.bus_hz);
	m->found = run(m, "A1", "A4") == 0;
	logf_(m, "ms7210: chip ID %02X %02X %02X (%s)", m->chip_id[0], m->chip_id[1],
	      m->chip_id[2], m->found ? "found" : "not found");
	return m->found ? 0 : -1;
}

/* Choose HDMI or DVI for the monitor now attached. */
static void choose_mode(ms7210_t *m)
{
	if (m->cfg.mode == MS7210_MODE_HDMI) {
		m->dvi = 0;
		return;
	}
	if (m->cfg.mode == MS7210_MODE_DVI) {
		m->dvi = 1;
		return;
	}
	/* auto: a monitor whose EDID has no HDMI vendor block takes DVI;
	   no EDID, or a bad one, is treated as HDMI, as the vendor's code does.
	   That includes a good block 0 whose extension could not be read, or
	   was not a CEA-861 block with a good checksum: DVI there would lose
	   the sound on an HDMI set whose second block read badly. */
	if (m->hpd && ms7210_read_edid(m) == 0) {
		if (m->edid[0x7E] >= 1 && !m->edid_info.ext_ok) {
			logf_(m, "ms7210: EDID extension unreadable or not CEA-861: HDMI assumed");
			m->dvi = 0;
		} else {
			m->dvi = !m->edid_info.hdmi;
		}
	} else {
		m->dvi = 0;
	}
}

static unsigned meter_mhz100(uint16_t meter)
{
	return (unsigned) (meter * 2700u / 4096u);
}

static void report(ms7210_t *m)
{
	uint8_t v, lo, hi;

	m->pll_locked = check_pll(m);
	if (ms7210_read(m, REG_IN_STABLE, &v) == 0)
		m->input_stable = (v >> 4) & 1;
	if (ms7210_read(m, REG_HPD, &v) == 0)
		m->hpd = v & 1;
	/* the PCLK meter again, long after it was switched on (row C1 reads it
	   straight after B19): if the first reading was 0 only because the
	   meter had not finished a count, this one says so */
	if (ms7210_read(m, 0x001E, &lo) == 0 && ms7210_read(m, 0x001F, &hi) == 0)
		m->pclk_meter_late = lo | (hi << 8);
	logf_(m, "ms7210: %s, PLL %s after %d pass%s, input %s, monitor %s, PCLK meter "
	      "now %u (about %u.%02u MHz)",
	      m->dvi ? "DVI" : (m->cfg.audio ? "HDMI with sound" : "HDMI, no sound"),
	      m->pll_locked ? "locked" : "NOT locked", m->pll_tries,
	      m->pll_tries == 1 ? "" : "es",
	      m->input_stable ? "stable" : "not stable", m->hpd ? "attached" : "not attached",
	      m->pclk_meter_late, meter_mhz100(m->pclk_meter_late) / 100,
	      meter_mhz100(m->pclk_meter_late) % 100);
}

static int setup_output(ms7210_t *m)
{
	m->pll_tries = 1;
	int r = run(m, "E1", "E72");
	report(m);
	return r;
}

int ms7210_init(ms7210_t *m)
{
	uint8_t v;
	int r = 0;

	m->errors = 0;
	if (m->io->i2c_speed)
		m->io->i2c_speed(m->io->ctx, m->cfg.bus_hz);
	r |= run(m, "B1", "B22");
	r |= run(m, "C1", "C1");
	/* 6068 for 40 MHz with the 27 MHz RC trim (encoder.md row C1).  Read
	   straight after B19 switched the meter on: report() reads it again */
	logf_(m, "ms7210: PCLK meter %u, about %u.%02u MHz (40.00 expected)",
	      m->pclk_meter, meter_mhz100(m->pclk_meter) / 100,
	      meter_mhz100(m->pclk_meter) % 100);
	r |= run(m, "D1", "D15");
	m->hpd = ms7210_read(m, REG_HPD, &v) == 0 ? (v & 1) : 0;
	choose_mode(m);
	r |= setup_output(m);
	return r ? -1 : 0;
}

int ms7210_poll(ms7210_t *m)
{
	uint8_t v;
	int hpd;
	int d1 = row_index("D1");

	/* A chip that was reset under us (ESD as a cable goes in, a dip on its
	   1.2 V) has lost sections B to E, which only ms7210_init() writes:
	   0x120C holds D1's low byte from set-up on. */
	if (d1 >= 0) {
		if (ms7210_read(m, ms7210_rows[d1].reg, &v))
			return -1;
		if (v != (ms7210_rows[d1].arg & 0xFF)) {
			logf_(m, "ms7210: register 0x%04X reads 0x%02X, not 0x%02X: the chip "
			      "was reset; set up again", ms7210_rows[d1].reg, v,
			      ms7210_rows[d1].arg & 0xFF);
			ms7210_init(m);
			return 2;
		}
	}
	if (ms7210_read(m, REG_HPD, &v))
		return -1;
	hpd = v & 1;
	if (hpd == m->hpd)
		return 0;
	m->hpd = hpd;
	if (!hpd) {
		logf_(m, "ms7210: monitor unplugged");
		return 0;
	}
	logf_(m, "ms7210: monitor plugged in");
	choose_mode(m);
	setup_output(m);
	return 1;
}

static int edid_block(ms7210_t *m, uint8_t offset, uint8_t *buf)
{
	return m->io->i2c_write_read(m->io->ctx, MS7210_EDID_ADDR, &offset, 1, buf, 128);
}

int ms7210_read_edid(ms7210_t *m)
{
	int r, blocks = 0;

	memset(m->edid, 0, sizeof(m->edid));
	memset(&m->edid_info, 0, sizeof(m->edid_info));
	m->edid_ok = 0;
	/* DDC pass-through on for output 0, then the slow bus (encoder.md 8.6) */
	ms7210_write(m, REG_DDC_SEL, 0xFF);
	ms7210_write(m, REG_DDC_PASS, 0x0E);
	if (m->io->i2c_speed)
		m->io->i2c_speed(m->io->ctx, m->cfg.ddc_hz);
	r = edid_block(m, 0x00, m->edid);
	if (r == 0) {
		blocks = 1;
		/* one extension block is all a DOS board needs to look at; read
		   twice if the first try fails */
		if (m->edid[0x7E] >= 1 && (edid_block(m, 0x80, m->edid + 128) == 0 ||
					   edid_block(m, 0x80, m->edid + 128) == 0))
			blocks = 2;
	}
	ms7210_write(m, REG_DDC_PASS, 0x0F);
	if (m->io->i2c_speed)
		m->io->i2c_speed(m->io->ctx, m->cfg.bus_hz);

	if (!blocks) {
		logf_(m, "ms7210: no EDID (DDC did not answer)");
		return -1;
	}
	ms7210_edid_parse(m->edid, blocks * 128, &m->edid_info);
	m->edid_ok = m->edid_info.valid;
	if (!m->edid_ok) {
		logf_(m, "ms7210: EDID header or checksum wrong");
		return -1;
	}
	logf_(m, "ms7210: monitor %s %04X \"%s\": %s, %s, 800x600 at 60 Hz %s",
	      m->edid_info.vendor, m->edid_info.product, m->edid_info.name,
	      m->edid_info.hdmi ? "HDMI" :
	      (m->edid[0x7E] >= 1 && !m->edid_info.ext_ok) ? "extension block unreadable" :
	      "DVI (no HDMI vendor block)",
	      m->edid_info.basic_audio ? "basic audio" : "no audio",
	      (m->edid_info.est_800x600_60 || m->edid_info.std_800x600_60 ||
	       m->edid_info.dtd_800x600) ? "listed" : "NOT listed");
	return 0;
}

int ms7210_edid_parse(const uint8_t *e, int len, ms7210_edid_info_t *out)
{
	static const uint8_t header[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };

	memset(out, 0, sizeof(*out));
	if (len < 128 || memcmp(e, header, 8) != 0 || sum8(e, 128) != 0)
		return -1;
	out->valid = 1;
	out->blocks = 1;

	/* bytes 8 and 9: three letters of five bits, big-endian */
	uint16_t id = (e[8] << 8) | e[9];
	out->vendor[0] = '@' + ((id >> 10) & 0x1F);
	out->vendor[1] = '@' + ((id >> 5) & 0x1F);
	out->vendor[2] = '@' + (id & 0x1F);
	out->vendor[3] = 0;
	out->product = e[10] | (e[11] << 8);

	out->est_800x600_60 = e[0x23] & 0x01;
	for (int i = 0x26; i < 0x36; i += 2)
		if (e[i] == 0x45 && e[i + 1] == 0x40)   /* (800 / 8) - 31, 4:3, 60 Hz */
			out->std_800x600_60 = 1;
	for (int d = 0x36; d < 0x7E; d += 18) {
		const uint8_t *p = e + d;
		if (p[0] || p[1]) {
			int h = p[2] | ((p[4] & 0xF0) << 4);
			int v = p[5] | ((p[7] & 0xF0) << 4);
			if (h == 800 && v == 600)
				out->dtd_800x600 = 1;
		} else if (p[3] == 0xFC) {
			int k;
			for (k = 0; k < 13 && p[5 + k] != 0x0A; k++)
				out->name[k] = p[5 + k];
			out->name[k] = 0;
			while (k > 0 && out->name[k - 1] == ' ')
				out->name[--k] = 0;
		}
	}

	if (len >= 256 && e[0x7E] >= 1) {
		const uint8_t *x = e + 128;
		out->blocks = 2;
		if (x[0] == 0x02 && sum8(x, 128) == 0) {
			int dtd = x[2];
			out->ext_ok = 1;
			out->basic_audio = (x[1] >= 2) && (x[3] & 0x40);
			/* data block collection: bytes 4 to dtd - 1 */
			for (int i = 4; dtd <= 127 && i < dtd; ) {
				int tag = x[i] >> 5, n = x[i] & 0x1F;
				if (i + n >= dtd)
					break;
				if (tag == 3 && n >= 3 &&
				    x[i + 1] == 0x03 && x[i + 2] == 0x0C && x[i + 3] == 0x00)
					out->hdmi = 1;
				i += 1 + n;
			}
		}
	}
	return 0;
}
