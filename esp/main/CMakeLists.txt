if(NOT DEFINED BOARD)
    set(BOARD "jc3248w535")
endif()

# Boards with the boot console on the panel and the crash screen (their LCD
# file gives what lcd.h says), and boards built without Wi-Fi: wifi.c alone
# holds about 45 KB of internal RAM through the Wi-Fi libraries, even when
# Wi-Fi never starts.
set(TINY386_CONSOLE_BOARDS korvo1 dosboard)
set(TINY386_NO_WIFI_BOARDS korvo1 dosboard)

# v5.2 net: idf.py -DTINY386_WIFI=1, the Korvo-1's network build: wifi.c in
# (started only by [esp] ssid), sdkconfig.korvo1-wifi after the board's own
# (esp/CMakeLists.txt), which puts most of Wi-Fi's memory in PSRAM, and two
# of the board's RAM savers to make room for what is left: cpu_exec1()
# compiled for size (I386_EXEC1_SMALL, board_korvo1.h) and linker_hot.lf
# left out.  Even trimmed, Wi-Fi takes about 15 KB of internal RAM when it
# never starts, and more once it has (s31/NET.md).  Without TINY386_WIFI the
# Korvo-1's build is as before.
if(TINY386_WIFI)
    if(NOT BOARD STREQUAL "korvo1")
        message(FATAL_ERROR "TINY386_WIFI: the network build is for BOARD=korvo1")
    endif()
    list(REMOVE_ITEM TINY386_NO_WIFI_BOARDS korvo1)
    set(NO_HOT_IRAM 1)
endif()

set(srcs "esp_main.c"
"../../adlib.c"
"../../fmopl.c"
"../../fpu.c"
"../../i386.c"
"../../i8042.c"
"../../i8254.c"
"../../i8259.c"
"../../ide.c"
"../../ini.c"
"../../pc.c"
"../../misc.c"
"../../pci.c"
"../../vga.c"
"../../vgafont.c"
"../../ne2000.c"
"../../i8257.c"
"../../sb16.c"
"../../pcspk.c"
"../../perfport.c"
"bootmenu.c"
"console.c"
"dbuf_boot.c"
"disk_image.c"
"sd_part.c"
"display.c"
"hid_parse.c"
"kbd_input.c"
"ini_edit.c"
"setup.c"
"speed.c"
"profile.c"
"prof_hist.c"
"lcd_axs15231b.c"
"lcd_st7701.c"
"lcd_elecrow7s3.c"
"lcd_korvo1.c"
"sdmmc_crc_wrap.c"
"sdmmc_clk_wrap.c"
"sdmmc_setup_wrap.c"
"sdmmc_timeout_wrap.c"
"sdmmc_wait_wrap.c"
"sdmmc_read_wrap.c"
"i2s.c"
"audio_mix.c"
"storage.c"
"usb_hid.c")
if(NOT BOARD IN_LIST TINY386_NO_WIFI_BOARDS)
    list(APPEND srcs "wifi.c")
endif()
if(BOARD IN_LIST TINY386_CONSOLE_BOARDS)
    list(APPEND srcs "crash.c")
endif()
# The status LED (STATUS_LED_GPIO in the board header): both boards have a
# WS2812-protocol LED on GPIO37.
set(TINY386_STATUS_LED_BOARDS korvo1 dosboard)
if(BOARD IN_LIST TINY386_STATUS_LED_BOARDS)
    list(APPEND srcs "status_code.c" "status_led.c")
endif()
# The DOS board: its panel file, the board check and HDMI start-up, the
# MS7210 and CH9350L drivers.  Its keyboard and mouse come through the
# CH9350L, so the S31's own USB host (usb_input.c) is not built for it.
if(BOARD STREQUAL "dosboard")
    list(APPEND srcs "lcd_dosboard.c" "dosboard.c" "ms7210.c" "ch9350.c")
else()
    list(APPEND srcs "usb_input.c")
endif()

# v5.2: the code core 1 runs from flash most, in internal RAM (linker_hot.lf);
# the S31 boards only, -DNO_HOT_IRAM=1 leaves it out
set(ldfrags "")
if(IDF_TARGET STREQUAL "esp32s31" AND NOT NO_HOT_IRAM)
    set(ldfrags "linker_hot.lf")
endif()

idf_component_register(SRCS ${srcs}
INCLUDE_DIRS ""
LDFRAGMENTS ${ldfrags})

message(STATUS "Building for BOARD=${BOARD}")

add_definitions(-include ${CMAKE_CURRENT_SOURCE_DIR}/board_${BOARD}.h)

# The emulator's own sources are outside the project directory, where
# ESP-IDF's path maps do not reach.  Map them too, so that the file names
# that assert() keeps and the debug information do not depend on where the
# tree is: with CONFIG_APP_REPRODUCIBLE_BUILD two builds of the same sources
# in different directories then give the same bytes.
get_filename_component(TINY386_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_compile_options(${COMPONENT_LIB} PRIVATE "-ffile-prefix-map=${TINY386_ROOT}=/TINY386")

if(BOARD IN_LIST TINY386_NO_WIFI_BOARDS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_WIFI)
endif()
if(TINY386_WIFI)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE I386_EXEC1_SMALL)
    # v5.3: lwIP's tcpip task with its stack in PSRAM (wifi.c)
    target_link_options(${COMPONENT_LIB} INTERFACE
        -Wl,--wrap=sys_thread_new)
endif()

# Every panic, abort and watchdog goes through the crash screen first
# (crash.c), which then calls ESP-IDF's own handler.  With the status LED,
# the halt at the end of it runs the LED's crash pattern (crash.c).
if(BOARD IN_LIST TINY386_CONSOLE_BOARDS)
    target_link_options(${COMPONENT_LIB} INTERFACE
        -Wl,--wrap=esp_panic_handler)
endif()
if(BOARD IN_LIST TINY386_STATUS_LED_BOARDS)
    target_link_options(${COMPONENT_LIB} INTERFACE
        -Wl,--wrap=esp_system_reset_modules_on_exit)
endif()

# idf.py -DCRASH_TEST=1: a test build that reads [esp] crash_test from the
# ini and crashes on purpose (esp_main.c).  Not for the release images.
if(CRASH_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TINY386_CRASH_TEST)
endif()

# v5.2, the S31 boards: the SD clock kept between commands (sdmmc_clk_wrap.c,
# [esp] sd_clock_fix), and the memory benchmark ([esp] membench, membench.c).
# v5.3: a card command that timed out reported as one, which this ESP-IDF
# reports as done when its buffer is in PSRAM (sdmmc_timeout_wrap.c); the
# slot's other settings kept between commands too (sdmmc_setup_wrap.c), and
# after a write the card asked again as it lets go of D0 (sdmmc_wait_wrap.c),
# and no status command after a read (sdmmc_read_wrap.c).
if(IDF_TARGET STREQUAL "esp32s31")
    target_sources(${COMPONENT_LIB} PRIVATE "membench.c" "membench_pie.S")
    target_link_options(${COMPONENT_LIB} INTERFACE
        -Wl,--wrap=sd_host_slot_get_real_freq
        -Wl,--undefined=__wrap_sd_host_slot_get_real_freq
        -Wl,--wrap=sdmmc_host_do_transaction
        -Wl,--undefined=__wrap_sdmmc_host_do_transaction
        -Wl,--wrap=sd_host_slot_set_card_clk
        -Wl,--undefined=__wrap_sd_host_slot_set_card_clk
        -Wl,--wrap=sd_host_slot_set_bus_width
        -Wl,--undefined=__wrap_sd_host_slot_set_bus_width
        -Wl,--wrap=sd_host_slot_set_bus_sampling_mode
        -Wl,--undefined=__wrap_sd_host_slot_set_bus_sampling_mode
        -Wl,--wrap=sd_host_set_delay_phase
        -Wl,--undefined=__wrap_sd_host_set_delay_phase
        -Wl,--wrap=sd_host_set_delay_line
        -Wl,--undefined=__wrap_sd_host_set_delay_line
        -Wl,--wrap=sdmmc_wait_for_idle
        -Wl,--undefined=__wrap_sdmmc_wait_for_idle
        -Wl,--wrap=sdmmc_read_sectors
        -Wl,--undefined=__wrap_sdmmc_read_sectors)
endif()

# Linker wrap for SPI SD CMD59 workaround (elecrow7s3 only)
if(BOARD STREQUAL "elecrow7s3")
    target_link_options(${COMPONENT_LIB} INTERFACE
        -Wl,--wrap=sdmmc_init_spi_crc
        -Wl,--undefined=__wrap_sdmmc_init_spi_crc)
endif()

# i8042.c hands the guest's keyboard lights and typematic rate to
# kbd_input.c (the Linux route builds it without).
add_definitions(-DPS2_KBD_HOOKS)
add_definitions(-DI386_ENABLE_FPU)
add_definitions(-DUSE_FMOPL)
# the information port at 0F1F8h (perfport.h, perfport.c; speed.c fills the
# board's part)
add_definitions(-DTINY386_PERFPORT)

set_source_files_properties("../../adlib.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../fmopl.c" PROPERTIES COMPILE_FLAGS "-Wno-all -fsingle-precision-constant")
set_source_files_properties("../../fpu.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../i386.c" PROPERTIES COMPILE_FLAGS "-Wno-all -O3 -freorder-blocks-algorithm=simple -fno-tree-tail-merge")
set_source_files_properties("../../i8042.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../vga.c" PROPERTIES COMPILE_FLAGS "-Wno-all -O3")
set_source_files_properties("../../pc.c" PROPERTIES COMPILE_FLAGS "-Wno-all -O3")
set_source_files_properties("../../ne2000.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../sb16.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../i8257.c" PROPERTIES COMPILE_FLAGS "-Wno-all")

if(NOT EXISTS "../../fmopl.inc")
    message(FATAL_ERROR "Please run `make prepare` first")
endif()
