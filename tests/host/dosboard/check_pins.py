#!/usr/bin/env python3
"""
The DOS board's pins as esp/main/board_dosboard.h gives them, against the
board's own netlist (tiny386-hdmi-board dosboard/out/dosboard.net, KiCad 9,
rev A2c on): every pin the firmware names is on the net it should be, every
GPIO-like name in the header is one this script knows, and no pin the
firmware names is one the board puts on the J202 header, except the I2C bus
(GPIO0, GPIO1), which J202 shares by design.  Prints the table it checked.

    python3 check_pins.py BOARD_HEADER NETLIST

Exit status 0 when every check passes, 1 when one fails, 2 when a file
cannot be read as expected.
"""
import re
import sys

# board_dosboard.h name: the net its GPIO must be on (the netlist's name,
# less any sheet path)
EXPECT = {
    "HDMI_I2C_SDA": "I2C_SDA",
    "HDMI_I2C_SCL": "I2C_SCL",
    "HDMI_RSTN_GPIO": "HDMI_RSTN",
    "HDMI_INT_GPIO": "HDMI_INT",
    "I2S_BCLK": "I2S_BCLK",
    "I2S_WS": "I2S_WS",
    "I2S_DOUT": "I2S_DOUT",
    "AUDIO_MUTE_GPIO": "AUD_XSMT",
    "KM_RX_GPIO": "KM_RX",
    "KM_TX_GPIO": "KM_TX",
    "KM_RST_GPIO": "KM_RST",
    "KM_S0_GPIO": "KM_S0",
    "USB_VBUS_EN_GPIO": "USB_VBUS_EN",
    "STATUS_LED_GPIO": "LED_STATUS",
    "USER_KEY_GPIO": "USER_BTN",
    "SD_PWR_EN_GPIO": "SD_PWR_EN_N",
    "SD_CD_GPIO": "SD_CD_N",
    "SD_CLK": "SD_CLK",
    "SD_CMD": "SD_CMD",
    "SD_D0": "SD_D0",
    "SD_D1": "SD_D1",
    "SD_D2": "SD_D2",
    "SD_D3": "SD_D3",
    "LCD_PCLK_GPIO": "LCD_PCLK_PAD",
    "LCD_DE_GPIO": "LCD_DE",
    "LCD_HSYNC_GPIO": "LCD_HSYNC",
    "LCD_VSYNC_GPIO": "LCD_VSYNC",
}
# LCD_DATA_GPIOS, in order: LCD_D0 to LCD_D15
DATA_LIST = "LCD_DATA_GPIOS"
# names that look like pins but are not GPIO numbers
NOT_PINS = {"I2S_NUM", "I2S_MCLK", "HDMI_I2C_PORT", "KM_UART_NUM", "HDMI_I2C_ADDR",
            "HDMI_I2C_HZ", "HDMI_DDC_HZ", "KM_BAUD", "SD_PWR_EN"}
LOOKS_LIKE_PIN = re.compile(r"(_GPIO$|_GPIOS$|^I2S_(BCLK|WS|DOUT|DIN|MCLK)$|^SD_(CLK|CMD|D\d)$|_SDA$|_SCL$)")
SHARED_WITH_HEADER = {0, 1}     # the I2C bus, on J202 pins 5 and 6 by design
HEADER = "J202"
MODULE_PIN = re.compile(r"GPIO(\d+)")


def read_header(path):
    text = open(path).read()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = text.replace("\\\n", " ")
    defs = {}
    for line in text.split("\n"):
        m = re.match(r"\s*#\s*define\s+(\w+)\s*(.*)$", line)
        if m:
            defs[m.group(1)] = m.group(2).strip()
    return defs


def read_netlist(path):
    """{gpio: (net, [J202 pins])} for the module's GPIO pads"""
    text = open(path).read()
    i = text.find("(nets")
    if i < 0:
        raise ValueError("no (nets in " + path)
    nets = []
    for chunk in text[i:].split("(net (code ")[1:]:
        m = re.search(r'\(name "([^"]*)"\)', chunk)
        if not m:
            continue
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)(?: \(pinfunction "([^"]*)"\))?', chunk)
        nets.append((m.group(1).split("/")[-1], nodes))
    # the module: the part with the most pins named GPIOn (U201 on rev A2c)
    count = {}
    for net, nodes in nets:
        for ref, pad, fn in nodes:
            if MODULE_PIN.search(fn or ""):
                count[ref] = count.get(ref, 0) + 1
    if not count:
        raise ValueError("no pin named GPIOn in " + path)
    module = max(count, key=count.get)
    gpios = {}
    for net, nodes in nets:
        header = [p for r, p, f in nodes if r == HEADER]
        for ref, pad, fn in nodes:
            if ref != module or not MODULE_PIN.search(fn or ""):
                continue
            g = int(MODULE_PIN.search(fn).group(1))
            if g in gpios:
                raise ValueError("GPIO%d on two nets" % g)
            gpios[g] = (net, ref, pad, header)
    return gpios


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    try:
        defs = read_header(sys.argv[1])
        gpios = read_netlist(sys.argv[2])
    except (OSError, ValueError) as e:
        print("check_pins: %s" % e)
        return 2
    if len(gpios) < 40:
        print("check_pins: only %d module GPIOs found in the netlist" % len(gpios))
        return 2
    fails = 0
    used = {}           # gpio: name

    def ok(cond, msg):
        nonlocal fails
        print(("  ok    " if cond else "  FAIL  ") + msg)
        if not cond:
            fails += 1

    print("board_dosboard.h against %s (%d module GPIOs on nets)" % (sys.argv[2], len(gpios)))
    # every pin-like name is one this script checks
    for name in sorted(defs):
        if LOOKS_LIKE_PIN.search(name) and name not in EXPECT and name != DATA_LIST and name not in NOT_PINS:
            ok(False, "%s = %s: a pin name this script does not know (add it to EXPECT)" % (name, defs[name]))
    for name, want in EXPECT.items():
        if name not in defs:
            ok(False, "%s: not in the header" % name)
            continue
        try:
            g = int(defs[name], 0)
        except ValueError:
            ok(False, "%s = %s: not a number" % (name, defs[name]))
            continue
        used.setdefault(g, []).append(name)
        net = gpios.get(g, ("(no net)", "", "", []))
        ok(net[0] == want, "%-17s GPIO%-2d module pad %-3s net %-13s (want %s)" % (name, g, net[2], net[0], want))
    if DATA_LIST in defs:
        pins = [int(x, 0) for x in re.split(r"[,\s]+", defs[DATA_LIST]) if x]
        ok(len(pins) == 16, "%s: %d pins" % (DATA_LIST, len(pins)))
        for k, g in enumerate(pins):
            used.setdefault(g, []).append("%s[%d]" % (DATA_LIST, k))
            net = gpios.get(g, ("(no net)", "", "", []))
            ok(net[0] == "LCD_D%d" % k, "%s[%-2d] GPIO%-2d module pad %-3s net %-13s (want LCD_D%d)"
               % (DATA_LIST, k, g, net[2], net[0], k))
    else:
        ok(False, "%s: not in the header" % DATA_LIST)
    for g, names in sorted(used.items()):
        ok(len(names) == 1, "GPIO%d named once (%s)" % (g, ", ".join(names)))
    # the header
    on_header = sorted(g for g, v in gpios.items() if v[3])
    print("  J202 carries GPIO %s" % ", ".join(str(g) for g in on_header))
    for g in on_header:
        net, ref, pad, hp = gpios[g]
        if g in SHARED_WITH_HEADER:
            ok(g in used, "GPIO%-2d J202 pin %s net %s: the shared I2C bus, the firmware's (%s)"
               % (g, ",".join(hp), net, ", ".join(used.get(g, ["nothing"]))))
        else:
            ok(g not in used, "GPIO%-2d J202 pin %s net %s: left to the header user, the firmware names it %s"
               % (g, ",".join(hp), net, "nowhere" if g not in used else "as " + ", ".join(used[g])))
    for g in range(2, 8):
        ok(g in on_header and g not in used, "GPIO%d (an LP core pin) on J202 and free" % g)
    print("check_pins: %d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
