/* Checks v3\card\tiny386 against the firmware's own setup.c and tiny386's
   ini.c on a pretend card: the boot menu's list and default, every key
   known to esp_main.c, every file named there. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "ini.h"
#include "setup.h"

static const char *const known_keys[] = {   /* esp_main.c, release build */
	"esp:ssid", "esp:pass", "esp:enable_usb", "esp:console",
	"esp:lcd_invalidate",
	"pc:bios", "pc:vga_bios", "pc:mem_size", "pc:vga_mem_size",
	"pc:hda", "pc:hdb", "pc:hdc", "pc:hdd",
	"pc:cda", "pc:cdb", "pc:cdc", "pc:cdd", "pc:fda", "pc:fdb",
	"pc:fill_cmos", "pc:linuxstart", "pc:kernel", "pc:initrd",
	"pc:cmdline", "pc:enable_serial", "pc:vga_force_8dm",
	"display:width", "display:height",
	"cpu:gen", "cpu:fpu", "cpu:cpuid_vendor",
	NULL
};
static const char *root, *dir;
static struct setup_map maps[2];
static int bad;

static int handler(void *u, const char *s, const char *n, const char *v)
{
	char key[64], out[300], full[400];
	bool missing;
	snprintf(key, sizeof(key), "%s:%s", s, n);
	int known = 0;
	for (int i = 0; known_keys[i]; i++)
		known |= !strcmp(known_keys[i], key);
	printf("    [%s] %s = %s%s", s, n, v, known ? "" : "   UNKNOWN KEY");
	bad += !known;
	/* v4.4: hda = sd:N is a card partition, not a file: esp_main.c's parse_ini()
	   leaves it unresolved and check_card_partition() checks it on the card */
	if (setup_is_path_key(s, n) && !strncmp(v, "sd:", 3)) {
		int ok = v[3] >= '1' && v[3] <= '4' && !v[4] && n[0] == 'h' && n[1] == 'd';
		printf("   -> card partition %s%s\n", v + 3, ok ? " (a partition, checked when the PC starts)" : " MALFORMED");
		bad += !ok;
		return 1;
	}
	if (setup_is_path_key(s, n)) {
		setup_resolve(dir, v, maps, 2, out, sizeof(out), &missing);
		/* absolute paths of the board, under the pretend card's root */
		if (out[0] == '/' && strncmp(out, root, strlen(root)))
			snprintf(full, sizeof(full), "%s%s", root, out);
		else
			snprintf(full, sizeof(full), "%s", out);
		int there = access(full, R_OK) == 0;
		printf("   -> %s%s", out + (strncmp(out, root, strlen(root)) ? 0 : strlen(root)),
		       there ? " (there)" : " MISSING");
		bad += !there || missing;
	}
	printf("\n");
	return 1;
}

int main(int argc, char **argv)
{
	static char d1[256], d2[256], m1[256], m2[256], p[300];
	root = argv[1];
	snprintf(d1, sizeof(d1), "%s/sdcard/tiny386", root);
	snprintf(d2, sizeof(d2), "%s/sdcard", root);
	snprintf(m1, sizeof(m1), "%s/sdcard", root);
	snprintf(m2, sizeof(m2), "%s/sdcard3", root);
	maps[0] = (struct setup_map) { "/mnt/sd", m1 };
	maps[1] = (struct setup_map) { "/mnt/win", m2 };
	struct setup_place places[] = { { d1, NULL }, { d2, "tiny386.ini" }, { NULL, NULL } };
	static struct setup_list l;
	int n = setup_find(places, &l);
	printf("boot menu: %d inis in %s\n", n, l.dir + strlen(root));
	for (int i = 0; i < l.n; i++)
		printf("  %d %-16s %s\n", i + 1, l.ini[i].name, l.ini[i].desc);
	int def = setup_default(&l, NULL);
	printf("default without lastboot.txt: %s\n", l.ini[def].name);
	dir = l.dir;
	for (int i = 0; i < l.n; i++) {
		setup_path(&l, i, p, sizeof(p));
		printf("  %s:\n", l.ini[i].name);
		int r = ini_parse(p, handler, NULL);
		printf("    ini_parse: %d\n", r);
		bad += r != 0;
	}
	printf("cardcheck: %d problems\n", bad);
	return bad != 0;
}
