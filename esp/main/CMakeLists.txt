idf_component_register(SRCS "esp_main.c"
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
"wifi.c"
"lcd_axs15231b.c"
"lcd_st7701.c"
"lcd_elecrow7s3.c"
"lcd_korvo1.c"
"sdmmc_crc_wrap.c"
"i2s.c"
"storage.c"
"usb_hid.c"
"usb_input.c"
INCLUDE_DIRS "")

if(NOT DEFINED BOARD)
    set(BOARD "jc3248w535")
endif()

message(STATUS "Building for BOARD=${BOARD}")

add_definitions(-include ${CMAKE_CURRENT_SOURCE_DIR}/board_${BOARD}.h)

# The emulator's own sources are outside the project directory, where
# ESP-IDF's path maps do not reach.  Map them too, so that the file names
# that assert() keeps and the debug information do not depend on where the
# tree is: with CONFIG_APP_REPRODUCIBLE_BUILD two builds of the same sources
# in different directories then give the same bytes.
get_filename_component(TINY386_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_compile_options(${COMPONENT_LIB} PRIVATE "-ffile-prefix-map=${TINY386_ROOT}=/TINY386")

# Linker wrap for SPI SD CMD59 workaround (elecrow7s3 only)
if(BOARD STREQUAL "elecrow7s3")
    target_link_options(${COMPONENT_LIB} INTERFACE
        -Wl,--wrap=sdmmc_init_spi_crc
        -Wl,--undefined=__wrap_sdmmc_init_spi_crc)
endif()

add_definitions(-DI386_ENABLE_FPU)
add_definitions(-DUSE_FMOPL)

set_source_files_properties("../../adlib.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../fmopl.c" PROPERTIES COMPILE_FLAGS "-Wno-all -fsingle-precision-constant")
set_source_files_properties("../../fpu.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../i386.c" PROPERTIES COMPILE_FLAGS "-Wno-all -O3")
set_source_files_properties("../../i8042.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../vga.c" PROPERTIES COMPILE_FLAGS "-Wno-all -O3")
set_source_files_properties("../../pc.c" PROPERTIES COMPILE_FLAGS "-Wno-all -O3")
set_source_files_properties("../../ne2000.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../sb16.c" PROPERTIES COMPILE_FLAGS "-Wno-all")
set_source_files_properties("../../i8257.c" PROPERTIES COMPILE_FLAGS "-Wno-all")

if(NOT EXISTS "../../fmopl.inc")
    message(FATAL_ERROR "Please run `make prepare` first")
endif()
