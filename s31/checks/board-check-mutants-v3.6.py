#!/usr/bin/env python3
"""Faults put into copies of dosboard.c (v3.6) for host test 18 to catch.
argv: dosboard.c outdir.  Writes outdir/NAME.c for each fault, and
outdir/list.txt with one "NAME<TAB>what" line each."""
import os, sys

src = open(sys.argv[1]).read()
out = sys.argv[2]
os.makedirs(out, exist_ok=True)

M = [
 ("no-pullup", "GPIO46 read with no pull-up (floating pins read as nothing pulls them)",
  "\t\t.pull_up_en = GPIO_PULLUP_ENABLE,\n\t};\n\tconst gpio_config_t plain",
  "\t};\n\tconst gpio_config_t plain"),
 ("pulldown", "GPIO46 read with the pull-down in place of the pull-up",
  "\t\t.pull_up_en = GPIO_PULLUP_ENABLE,\n\t};\n\tconst gpio_config_t plain",
  "\t\t.pull_down_en = GPIO_PULLDOWN_ENABLE,\n\t};\n\tconst gpio_config_t plain"),
 ("not-restored", "GPIO46 left with its pull-up on",
  "\tgpio_config(&plain);\n\treturn low;", "\t(void) plain;\n\treturn low;"),
 ("inverted", "the strap's sense inverted",
  "\t\tif (!s_strap)\n\t\t\twrong_board();", "\t\tif (s_strap)\n\t\t\twrong_board();"),
 ("no-gate", "no strap gate: the reset tried whatever GPIO46 reads",
  "\t\tif (!s_strap)\n\t\t\twrong_board();", "\t\t(void) 0;"),
 ("v35-stop", "a Korvo-1 address stops the check even with GPIO46 low (v3.5's rule)",
  "\t\tif (!s_strap)\n\t\t\twrong_board();", "\t\tif (!s_strap || s_korvo1_addr >= 0)\n\t\t\twrong_board();"),
 ("strap-first", "GPIO46 read before the first probe, on every board",
  "\ts_found_unreset = probe_ms7210() == 0;", "\ts_strap = s0_strap_low();\n\ts_found_unreset = probe_ms7210() == 0;"),
 ("output46", "GPIO46 read as an input and output",
  "\t\t.pin_bit_mask = 1ULL << KM_S0_GPIO,\n\t\t.mode = GPIO_MODE_INPUT,\n\t\t.pull_up_en = GPIO_PULLUP_ENABLE,",
  "\t\t.pin_bit_mask = 1ULL << KM_S0_GPIO,\n\t\t.mode = GPIO_MODE_INPUT_OUTPUT,\n\t\t.pull_up_en = GPIO_PULLUP_ENABLE,"),
 ("rstn-order", "RSTN made an output before it is set high",
  "\tgpio_set_level(HDMI_RSTN_GPIO, 1);\n\tgpio_config(&rstn);",
  "\tgpio_config(&rstn);\n\tgpio_set_level(HDMI_RSTN_GPIO, 1);"),
 ("rstn-early", "RSTN taken before the first probe (as v3.4 did)",
  "\ts_found_unreset = probe_ms7210() == 0;",
  "\tgpio_set_level(HDMI_RSTN_GPIO, 1);\n\tgpio_config(&rstn);\n\ts_rstn_ours = true;\n\ts_found_unreset = probe_ms7210() == 0;"),
 ("mute-order", "GPIO48 made an output before it is set low",
  "\tgpio_set_level(AUDIO_MUTE_GPIO, 0);      /* line out and speakers off */\n\tgpio_config(&outs);",
  "\tgpio_config(&outs);\n\tgpio_set_level(AUDIO_MUTE_GPIO, 0);      /* line out and speakers off */"),
 ("mute-high", "GPIO48 set high by the board check",
  "\tgpio_config(&outs);\n\tgpio_set_level(AUDIO_MUTE_GPIO, 0);\n",
  "\tgpio_config(&outs);\n\tgpio_set_level(AUDIO_MUTE_GPIO, 1);\n"),
 ("msg-strap", "the halt message without GPIO46",
  "\"R302, so not a DOS board\"", "\"R302\""),
 ("no-note", "no console note for a device on J202",
  "\tif (s_korvo1_addr >= 0)\n\t\tdosboard_note(", "\tif (0)\n\t\tdosboard_note("),
 ("one-read-high", "a single high read among low ones ignored (any low read taken as low)",
  "\t\tif (gpio_get_level(KM_S0_GPIO))\n\t\t\tlow = 0;",
  "\t\tif (gpio_get_level(KM_S0_GPIO) && i == 3)\n\t\t\tlow = 0;"),
 ("no-reset-46", "no strap read at all, the fallback as v3.5 had it (Korvo-1 addresses stop it)",
  "\t\ts_strap = s0_strap_low();\n\t\tif (!s_strap)\n\t\t\twrong_board();",
  "\t\tif (s_korvo1_addr >= 0)\n\t\t\twrong_board();\n\t\ts_strap = 1;"),
]

with open(os.path.join(out, "list.txt"), "w") as lst:
    for name, what, old, new in M:
        n = src.count(old)
        if n != 1:
            print("pattern for %s found %d times" % (name, n))
            sys.exit(1)
        open(os.path.join(out, name + ".c"), "w").write(src.replace(old, new))
        lst.write("%s\t%s\n" % (name, what))
print(len(M), "faults written")
