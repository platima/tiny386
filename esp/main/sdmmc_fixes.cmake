# sdmmc_fixes.cmake (v5.3): whether the ESP-IDF a build is made against
# has the two upstream SD fixes that sdmmc_timeout_wrap.c stands in for,
# read from that ESP-IDF's own source:
#
#   e00861c3f497, "fix(sdmmc): do not let cache sync mask a failed
#   transaction", in components/esp_driver_sdmmc/src/sd_trans_sdmmc.c
#
#   007945125db7, "fix(sdmmc): Dont overwrite CMD25 err with ACMD22 result
#   in sdmmc_write_sectors_dma", in components/sdmmc/sdmmc_cmd.c
#
# The test is on the fixed code itself, not on a version number, and it is
# strict.  A fix counts as there only when its fixed lines are in the file
# as upstream wrote them (white space aside), the call it is about is in
# the file once, inside those lines, and the variable it brought in is used
# nowhere else.  Anything else counts as not fixed: a missing file, a file
# laid out differently, the old line left beside the new ones.  A fix taken
# as there when it is not would let a read or write that failed be
# reported as done; one taken as missing when it is there only costs the
# wrap's part for it, a few hundred bytes of internal RAM.
#
# tiny386_sdmmc_fixes(IDF_PATH SYNC_FIXED CMD25_FIXED) sets SYNC_FIXED
# (e00861c3f497) and CMD25_FIXED (007945125db7) to TRUE or FALSE in the
# caller's scope.  esp/main/CMakeLists.txt calls it, and so does
# tests/host/sdmmc/test_sdmmc_fixes.cmake (cmake -P).

# the file's text with each run of white space made one space; empty when
# there is no such file
function(_tiny386_squeezed path out)
    set(text "")
    if(EXISTS "${path}" AND NOT IS_DIRECTORY "${path}")
        file(READ "${path}" text)
        string(REGEX REPLACE "[ \t\r\n]+" " " text "${text}")
    endif()
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# how many times NEEDLE is in TEXT
function(_tiny386_count text needle out)
    set(n 0)
    string(LENGTH "${needle}" len)
    string(FIND "${text}" "${needle}" at)
    while(at GREATER -1)
        math(EXPR n "${n} + 1")
        math(EXPR at "${at} + ${len}")
        string(SUBSTRING "${text}" ${at} -1 text)
        string(FIND "${text}" "${needle}" at)
    endwhile()
    set(${out} ${n} PARENT_SCOPE)
endfunction()

# TRUE when TEXT has CALL once, FIXED (which holds CALL) once, and NAME
# exactly USES times, which are FIXED's own uses of it
function(_tiny386_has_fix text call fixed name uses out)
    _tiny386_count("${text}" "${call}" n_call)
    _tiny386_count("${text}" "${fixed}" n_fixed)
    _tiny386_count("${text}" "${name}" n_name)
    _tiny386_count("${fixed}" "${name}" n_own)
    if(n_call EQUAL 1 AND n_fixed EQUAL 1 AND n_name EQUAL uses AND n_own EQUAL uses)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(tiny386_sdmmc_fixes idf_path sync_out cmd25_out)
    # e00861c3f497: the cache sync after the transfer keeps its result
    # apart, and takes the place of the transfer's only when that was good
    _tiny386_squeezed("${idf_path}/components/esp_driver_sdmmc/src/sd_trans_sdmmc.c" text)
    _tiny386_has_fix("${text}"
        "esp_cache_msync((void *)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C)"
        "esp_err_t sync_ret = esp_cache_msync((void *)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C); if (ret == ESP_OK) { ret = sync_ret; } else if (sync_ret != ESP_OK) { ESP_LOGE(TAG, \"cache sync failed: 0x%x\", sync_ret); } if (ret != ESP_OK) { goto out; }"
        "sync_ret" 4 sync)
    # 007945125db7: a failed CMD25's count of blocks written (ACMD22) keeps
    # its result apart, so sdmmc_write_sectors_dma() returns the write's
    _tiny386_squeezed("${idf_path}/components/sdmmc/sdmmc_cmd.c" text)
    _tiny386_has_fix("${text}"
        "sdmmc_send_cmd_num_of_written_blocks(card, &successfully_written_blocks)"
        "size_t successfully_written_blocks = 0; esp_err_t err_acmd22 = sdmmc_send_cmd_num_of_written_blocks(card, &successfully_written_blocks); if (err_acmd22 == ESP_OK) { ESP_LOGD(TAG, \"%s: successfully wrote %zu blocks out of %zu\", __func__, successfully_written_blocks, block_count); } else { ESP_LOGE(TAG, \"%s: sdmmc_send_cmd_num_of_written_blocks returned 0x%x\", __func__, err_acmd22); }"
        "err_acmd22" 3 cmd25)
    set(${sync_out} ${sync} PARENT_SCOPE)
    set(${cmd25_out} ${cmd25} PARENT_SCOPE)
endfunction()
