# The Korvo-1's network build (v5.2 net, 2026-11-10)

The Korvo-1's images have always been built without Wi-Fi, to keep internal RAM. This build puts it back
in, so that the guest's NE2000 (I/O 300h, IRQ 9, `pc.c`) reaches a 2.4 GHz access point such as a phone's
hotspot. It is a build of its own, `v5.2-net`; the ordinary Korvo-1 and DOS board images are not changed by
any of it (the same bytes as before, 2 below).

```sh
cd esp && idf.py -B build-korvo1-net -DBOARD=korvo1 -DTINY386_WIFI=1 -DIDF_TARGET=esp32s31 build
TINY386_NET=1 bash s31/build.sh WORKDIR     # the release way: the two dual-boot image sets only
```

In the ini, under `[esp]`:

```ini
ssid = the hotspot's name
pass = its password
```

Without `ssid` nothing of Wi-Fi starts. With it, the boot joins the access point and waits up to 10 s for an
address before the PC is built, saying on the boot console what it got (address, gateway, the MAC the guest
shares). If no address comes, the PC starts anyway and Wi-Fi keeps trying. An empty `pass` joins an open
access point.

## 1. What `-DTINY386_WIFI=1` changes

| Where | What |
|-|-|
| `esp/CMakeLists.txt` | `sdkconfig.korvo1-wifi` after `sdkconfig.korvo1`, in the build folder; version `v5.2-net` |
| `esp/sdkconfig.korvo1-wifi` | Wi-Fi's, lwIP's and mbedTLS's memory from PSRAM first, the `.bss` of lwIP, net80211 and pp in PSRAM, Wi-Fi's and lwIP's IRAM paths off, no NVS and no stored PHY calibration, a station only (no SoftAP, enterprise, DHCP server or IPv6), fewer buffers, the network's tasks on core 0; v5.3 (section 6): 4 static TX buffers, the default event loop and its queue in PSRAM, no 802.15.4 |
| `esp/main/CMakeLists.txt` | `wifi.c` in (no `NO_WIFI`), `I386_EXEC1_SMALL`, `linker_hot.lf` left out; v5.3: `sys_thread_new` wrapped, for lwIP's task in PSRAM |
| `esp/main/wifi.c` | the S31 parts below, behind `CONFIG_IDF_TARGET_ESP32S31`; the other boards' Wi-Fi is as it was |

`wifi.c` on the S31:

* **The guest's frames without patching ESP-IDF.** Elsewhere `esp-idf-6.0.patch` makes ESP-IDF's
  `wlanif.c` call `wlanif_l2_input_hook()` (`ne2000.c`); the S31 boards build with ESP-IDF as it is. So the
  station's receive callback is replaced: ESP-IDF registers `wifi_sta_receive()` at each
  `WIFI_EVENT_STA_CONNECTED`, and a handler registered after its own (handlers for one event run in the order
  registered, `esp_event.c`) registers `sta_rx()` in its place. `sta_rx()` gives each frame to
  `wlanif_l2_input_hook()`, the same filter as on the other boards (ARP and DHCP replies to both, other IPv4
  to the guest, TCP to port 9999 to the firmware's remote keyboard), frees what the guest alone takes, and
  passes the rest to `esp_netif_receive()`, which is what `wifi_sta_receive()` calls. Frames out are
  `esp_wifi_internal_tx()`, as before.
* **Nothing written to flash.** The dual-boot table's `nvs` and `phy_init` partitions are Linux's. Wi-Fi's
  NVS is off (`nvs_flash_init()` is not called, and with it off upstream's erase on a version change is gone
  too), and the radio is calibrated in full at each start rather than from stored data.
* **No wait for ever.** Upstream waits for an address with `portMAX_DELAY`; here 10 s, then the PC starts.
  A step of the start that fails is said on the console and the PC starts without a network, rather than
  the firmware stopping.
* The radio does not sleep between beacons (`WIFI_PS_NONE`), so frames to the guest do not wait for one.
  WPA3 is joined by either SAE method. The remote keyboard's task has its stack in PSRAM, and since v5.3 so
  have the Wi-Fi driver's task and lwIP's (section 6).

## 2. The ordinary builds

`korvo1`, `korvo1-standalone` and `dosboard`, built on this branch and on 0eadb1a (its parent) with the same
folder names: each `tiny386.bin` the same bytes. Nothing they compile was changed (`wifi.c` is not in them).

## 3. Internal RAM

Static, from `idf.py size` (DIRAM left; the Korvo-1 dual boot):

| Build | DIRAM left | against v52-test3 |
|-|-|-|
| v52-test3 (0eadb1a, no Wi-Fi) | 110,457 | |
| `wifi.c` in, ESP-IDF's Wi-Fi defaults | 61,657 | -48,800 |
| and the trims (PSRAM first, IRAM paths off, no NVS, station only, fewer buffers) | 77,803 | -32,654 |
| and the `.bss` of lwIP, net80211 and pp in PSRAM | 95,427 | -15,030 |
| and `I386_EXEC1_SMALL` | 120,511 | +10,054 |
| and `linker_hot.lf` out instead | 114,771 | +4,314 |
| both: the network build | 139,855 | +29,398 |
| v5.3: and 802.15.4 off (section 6) | 140,517 | +30,060 |

What Wi-Fi still holds in internal RAM when it never starts: libphy 4.2 KB (3 KB of it code the radio needs
in internal RAM), libpp 3.4 KB, libnet80211 1.7 KB, esp_hw_support 1.9 KB, the rest under 1 KB each.

On the board, v52-test3's `PC built.` line read 73 KiB least free (2026-11-10, `fast_cpu = 0`, `profile = 1`,
`hda = sd:3`). Scaled by the static figures [inferred]:

| Build | least free, Wi-Fi not started | Wi-Fi running (estimate) |
|-|-|-|
| Wi-Fi with the trims and `.bss` in PSRAM only | about 58 KiB: under the 64 KiB rule before Wi-Fi starts | |
| and `I386_EXEC1_SMALL` | about 83 KiB | about 53 to 63 KiB |
| and `linker_hot.lf` out | about 77 KiB | about 48 to 58 KiB |
| the network build (both) | about 102 KiB | about 72 to 82 KiB |

The estimate for Wi-Fi running is 20 to 30 KB of internal RAM [inferred, not measured]: the Wi-Fi task's
stack (6.5 KiB with WPA3), lwIP's task (3 KiB), the event loop's (2.25 KiB), their task blocks, the driver's
queues and semaphores, the supplicant (whose `malloc()` is internal, with `SPIRAM_USE_CAPS_ALLOC`), the PHY's
calibration while it runs, and the receive buffers if the driver keeps them in internal RAM after all. The
board's figure decides: the `PC built.` line comes after the join, and the address line has the least free
at that point too.

## 4. What the two savers cost

* `I386_EXEC1_SMALL` (board_korvo1.h): `cpu_exec1()` compiled for size. `fast_cpu = 0` takes about 6 per cent
  more host instructions (BENCH, v5.1's count); the fast interpreter's hand-overs are as quick within a
  tenth of a per cent. `test_fast_cpu` with it, host and the board's compiler under qemu: see BUILD-NOTES
  35.10.
* `linker_hot.lf` out: the hottest flash code goes back to flash. Estimated at v5.2 as 2 to 3 per cent in use
  and 3.5 to 4.5 per cent in the boot; never measured on its own on the board.

So this build is a little slower than v52-test3 with or without `ssid`. Once the board has given Wi-Fi's real
figure, one of the two may be enough.

## 5. What only the board can say

* Whether the S31's Wi-Fi joins at all under this ESP-IDF (v6.1-dev), and with its receive buffers in PSRAM.
* The least free with and without `ssid`.
* Whether Windows 95 gets an address and pings. The guest shares the board's MAC: the hotspot's DHCP server
  sees two clients with one MAC, and normally gives both the same address; ARP and DHCP replies go to both
  stacks. lwIP takes no ARP from its own MAC as a conflict; Windows 95's own check is untested.

## 6. v5.3: less internal RAM while Wi-Fi runs

A read of ESP-IDF and of the S31's Wi-Fi libraries (v5.3) puts what Wi-Fi holds in internal RAM once it runs at
about 45 KB rather than section 3's 20 to 30: the driver's static TX and RX buffers alone are about 24 KB of
internal DMA RAM. v52-net1 on the board, Wi-Fi joined, in Windows: 58 KiB free, 29 KiB largest block, 56 KiB
least, under the 64 KiB the firmware is meant to keep. v5.3 takes back what costs the guest nothing it would
notice. The figures are from where each allocation is made, not yet measured on the board:

| Change | Internal RAM back | What can be seen |
|-|-|-|
| 4 static TX buffers rather than 8 | 6.8 KB while Wi-Fi runs | every frame sent waits in one of the 16 cache buffers in PSRAM for one of the 4, as before for one of the 8; an aggregate the board sends holds at most 4 frames |
| the Wi-Fi driver's task with its stack in PSRAM (`wifi.c`) | 6.5 KiB while Wi-Fi runs | nothing, if it works; this is its first run |
| lwIP's tcpip task with its stack in PSRAM (`wifi.c`, `sys_thread_new` wrapped) | 3 KiB while Wi-Fi runs | nothing |
| the default event loop in PSRAM: its task's stack, the loop, the handlers' records | about 2.5 KB while Wi-Fi runs | nothing, if it works: its spinlock is in PSRAM too |
| no events posted from interrupts, so the loop's queue and each event's data in PSRAM, with room for 16 events rather than 32 | about 0.3 KB while Wi-Fi runs | nothing |
| no IEEE 802.15.4, so no software coexistence | 678 bytes for good; 7.5 KB of flash | nothing |
| with each address, the network tasks' least stack free | none | `Wi-Fi: least stack free so far (bytes): wifi N, tcpip N, sys_evt N, esp_timer N.` on the boot console |
| the address line's least free with the DMA-capable part beside it | none | `Internal RAM least free since start N KiB, DMA-capable M KiB.` at the end of the address and `back on` lines |

That is about 19 KB while Wi-Fi runs and 0.7 KB for good, beside v5.3's changes to both builds (the status
LED's and key repeat's stacks in PSRAM, 6 KiB, and for `hda = sd:N` the card's DMA straight into the disk
buffers, 6 KiB). Together, the 56 KiB least would be about 87 KiB [inferred]. The `PC built.` line now gives the
DMA-capable part of internal RAM too; the TX buffers, which these trims shrink most, are DMA-capable RAM.

Left out, as each costs the guest something or needs the board's figures first: 4 static RX buffers rather than
6, which needs A-MPDU receive off (about 3.3 KB; slower downloads in bursts); 2 static TX buffers (a further
3.4 KB); the esp_timer task's stack at 2048 as in the standard builds (1.5 KiB), until the line above shows its
margin; WPA3 off, which with the driver's stack in PSRAM would save flash only; and the remote keyboard on TCP
port 9999 only when asked for (about 0.5 KB, and a port that takes keys from anyone on the hotspot), which needs
an ini key in `esp_main.c`, shared with the standard builds.
