# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Board selection: -DBOARD=<name> via cmake/idf.py, or BOARD env var, or default jc3248w535.
# This must be set BEFORE include(project.cmake) so ESP-IDF reads the right sdkconfig.
if(NOT DEFINED BOARD)
    if(DEFINED ENV{BOARD})
        set(BOARD $ENV{BOARD})
    else()
        set(BOARD "jc3248w535")
    endif()
endif()
# Always sync sdkconfig from the board-specific file.
# This ensures switching boards (via -DBOARD=) just works without manual cleanup.
# If you use menuconfig, save changes back to sdkconfig.${BOARD}.
set(BOARD_SDKCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.${BOARD}")
# -DPARTITION_CSV=<file in this folder>: the board's sdkconfig with another
# partition table, and nothing else changed.  The Korvo-1's standalone image
# set is built this way with tiny386's own partitions.csv; its default
# table is the dual-boot one beside Linux (sdkconfig.korvo1).
if(DEFINED PARTITION_CSV AND EXISTS "${BOARD_SDKCONFIG}")
    file(READ "${BOARD_SDKCONFIG}" _tiny386_cfg)
    string(REGEX REPLACE "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"[^\"]*\""
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${PARTITION_CSV}\""
        _tiny386_cfg "${_tiny386_cfg}")
    file(WRITE "${CMAKE_BINARY_DIR}/sdkconfig.${BOARD}.table.tmp" "${_tiny386_cfg}")
    file(COPY_FILE "${CMAKE_BINARY_DIR}/sdkconfig.${BOARD}.table.tmp"
        "${CMAKE_BINARY_DIR}/sdkconfig.${BOARD}.table" ONLY_IF_DIFFERENT)
    set(BOARD_SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig.${BOARD}.table")
endif()
if(EXISTS "${BOARD_SDKCONFIG}")
    file(COPY_FILE "${BOARD_SDKCONFIG}" "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig" ONLY_IF_DIFFERENT)
endif()
set(SDKCONFIG_DEFAULTS "${BOARD_SDKCONFIG}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tiny386)

# The Korvo-1's dual-boot build lives beside Linux: its own flash_args would
# write tiny386's bootloader (no GPIO42 key) at 0x2000, the otadata at
# 0x17000 and tiny386 at 0x20000, over the Linux loader.  So "idf.py flash"
# and its relatives stop here, before esptool runs; the image sets and
# flash-dualboot.bat write the right places (FLASH.txt).
if(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME MATCHES "dualboot")
    add_custom_target(tiny386_no_flash
        COMMAND ${CMAKE_COMMAND} -E echo
            "tiny386: this is the dual-boot build, whose own flash_args would overwrite the Linux loader. Use flash-dualboot.bat or the commands in FLASH.txt."
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM)
    # dfu-flash writes the same three through USB DFU; otadata-flash and
    # erase-otadata only 0xFF over Linux's blank otadata, but are refused
    # too (boot safety review, round 2, note 4)
    foreach(t flash app-flash bootloader-flash partition-table-flash
              partition_table-flash encrypted-flash encrypted-app-flash
              erase_flash erase-flash dfu-flash otadata-flash erase-otadata
              erase_otadata)
        if(TARGET ${t})
            add_dependencies(${t} tiny386_no_flash)
        endif()
    endforeach()
endif()
