# v5.3: esp/main/sdmmc_fixes.cmake, which tells whether an ESP-IDF has the
# two upstream SD fixes that sdmmc_timeout_wrap.c stands in for, run on
# copies of the two files it reads (tests/host/run.sh 14d makes them):
#
#   OUT/188e3e55bb  sd_trans_sdmmc.c and sdmmc_cmd.c as 188e3e55bb has them
#   OUT/sync        the same with e00861c3f497 applied
#   OUT/cmd25       the same with 007945125db7 applied
#   OUT/both        the same with both
#
# Each must come out as it is, also with its two files named one by one as
# esp/main/CMakeLists.txt names them; and so must copies of the fixed
# files altered here in ways that leave a fault in, or the fix only in a
# comment, some with the live old line spelled another way, which must
# count as not fixed, and two with only white space changed, which must
# still count as fixed.  Then the ESP-IDF at IDF, for the record, checked
# when its files are the same as one of the copies.
#
#   cmake -DTREE=... -DOUT=... -DIDF=... -P test_sdmmc_fixes.cmake
#
# Exit status 0 when every case comes out as it should.

cmake_minimum_required(VERSION 3.16)
include(${TREE}/esp/main/sdmmc_fixes.cmake)

set(TRANS components/esp_driver_sdmmc/src/sd_trans_sdmmc.c)
set(CMD components/sdmmc/sdmmc_cmd.c)
set(fails 0)
set(cases 0)

# one ESP-IDF: what sdmmc_fixes.cmake makes of it against what it should
function(check name dir want_sync want_cmd25)
    tiny386_sdmmc_fixes("${dir}" sync cmd25)
    math(EXPR n "${cases} + 1")
    set(cases ${n} PARENT_SCOPE)
    if(sync STREQUAL want_sync AND cmd25 STREQUAL want_cmd25)
        message("  ok: ${name}: cache sync fix ${sync}, CMD25 fix ${cmd25}")
    else()
        message("  FAIL: ${name}: cache sync fix ${sync} (want ${want_sync}), "
            "CMD25 fix ${cmd25} (want ${want_cmd25})")
        math(EXPR n "${fails} + 1")
        set(fails ${n} PARENT_SCOPE)
    endif()
endfunction()

# the same for the two files named, as esp/main/CMakeLists.txt asks it
function(check_in name trans_c cmd_c want_sync want_cmd25)
    tiny386_sdmmc_fixes_in("${trans_c}" "${cmd_c}" sync cmd25)
    math(EXPR n "${cases} + 1")
    set(cases ${n} PARENT_SCOPE)
    if(sync STREQUAL want_sync AND cmd25 STREQUAL want_cmd25)
        message("  ok: ${name}: cache sync fix ${sync}, CMD25 fix ${cmd25}")
    else()
        message("  FAIL: ${name}: cache sync fix ${sync} (want ${want_sync}), "
            "CMD25 fix ${cmd25} (want ${want_cmd25})")
        math(EXPR n "${fails} + 1")
        set(fails ${n} PARENT_SCOPE)
    endif()
endfunction()

# a copy of OUT/both with the two files' text given (an empty one left out)
function(make_copy name trans cmd)
    file(REMOVE_RECURSE "${OUT}/${name}")
    if(NOT trans STREQUAL "")
        file(WRITE "${OUT}/${name}/${TRANS}" "${trans}")
    endif()
    if(NOT cmd STREQUAL "")
        file(WRITE "${OUT}/${name}/${CMD}" "${cmd}")
    endif()
endfunction()

# TEXT with FROM, which must be there once, changed to TO
function(altered text from to out)
    string(FIND "${text}" "${from}" at)
    string(FIND "${text}" "${from}" last REVERSE)
    if(at EQUAL -1 OR NOT at EQUAL last)
        message(FATAL_ERROR "the copy is not as this test expects: no single \"${from}\"")
    endif()
    string(REPLACE "${from}" "${to}" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

message("== the copies")
check("188e3e55bb" "${OUT}/188e3e55bb" FALSE FALSE)
check("188e3e55bb with e00861c3f497" "${OUT}/sync" TRUE FALSE)
check("188e3e55bb with 007945125db7" "${OUT}/cmd25" FALSE TRUE)
check("188e3e55bb with both" "${OUT}/both" TRUE TRUE)
check("no ESP-IDF there" "${OUT}/nothing-here" FALSE FALSE)
check_in("the two files named, from two copies" "${OUT}/both/${TRANS}" "${OUT}/188e3e55bb/${CMD}" TRUE FALSE)
check_in("the two files named, from two copies the other way" "${OUT}/188e3e55bb/${TRANS}" "${OUT}/both/${CMD}" FALSE TRUE)
check_in("no files named (neither component in the build)" "" "" FALSE FALSE)

file(READ "${OUT}/188e3e55bb/${TRANS}" old_trans)
file(READ "${OUT}/188e3e55bb/${CMD}" old_cmd)
file(READ "${OUT}/both/${TRANS}" trans)
file(READ "${OUT}/both/${CMD}" cmd)

set(I12 "            ")
set(I16 "                ")
set(sync_lines "${I12}esp_err_t sync_ret = esp_cache_msync((void *)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n")
set(sync_old "${I12}ret = esp_cache_msync((void *)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n")
set(sync_hunk "${sync_lines}${I12}if (ret == ESP_OK) {\n${I16}ret = sync_ret;\n${I12}} else if (sync_ret != ESP_OK) {\n${I16}ESP_LOGE(TAG, \"cache sync failed: 0x%x\", sync_ret);\n${I12}}\n${I12}if (ret != ESP_OK) {\n${I16}goto out;\n${I12}}\n")
set(cmd25_lines "${I12}esp_err_t err_acmd22 = sdmmc_send_cmd_num_of_written_blocks(card, &successfully_written_blocks);\n")
set(cmd25_old "${I12}err = sdmmc_send_cmd_num_of_written_blocks(card, &successfully_written_blocks);\n")
set(cmd25_hunk "${I12}size_t successfully_written_blocks = 0;\n${cmd25_lines}${I12}if (err_acmd22 == ESP_OK) {\n${I16}ESP_LOGD(TAG, \"%s: successfully wrote %zu blocks out of %zu\", __func__, successfully_written_blocks, block_count);\n${I12}} else {\n${I16}ESP_LOGE(TAG, \"%s: sdmmc_send_cmd_num_of_written_blocks returned 0x%x\", __func__, err_acmd22);\n${I12}}\n")

# the copies themselves are as upstream's fixes left them
altered("${trans}" "${sync_hunk}" "${sync_hunk}" t)
altered("${cmd}" "${cmd25_hunk}" "${cmd25_hunk}" t)

message("== e00861c3f497 altered: the cache sync fix must not count")
altered("${trans}" "${I12}if (ret == ESP_OK) {\n${I16}ret = sync_ret;" "${I12}if (ret != ESP_OK) {\n${I16}ret = sync_ret;" t)
make_copy(sync-turned "${t}" "${cmd}")
check("the guard turned round" "${OUT}/sync-turned" FALSE TRUE)
altered("${trans}" "sync_ret);\n${I12}}\n" "sync_ret);\n${I12}}\n${I12}ret = sync_ret;\n" t)
make_copy(sync-after "${t}" "${cmd}")
check("the sync's result taken again after the fixed lines" "${OUT}/sync-after" FALSE TRUE)
altered("${trans}" "${sync_lines}" "${sync_old}${sync_lines}" t)
make_copy(sync-beside "${t}" "${cmd}")
check("the old line left beside the new ones" "${OUT}/sync-beside" FALSE TRUE)
make_copy(sync-comment "/*\n${sync_hunk}*/\n${old_trans}" "${cmd}")
check("the fixed lines only in a comment, the old code below" "${OUT}/sync-comment" FALSE TRUE)
make_copy(sync-name "${trans}\n/* sync_ret */\n" "${cmd}")
check("sync_ret used outside the fixed lines too" "${OUT}/sync-name" FALSE TRUE)
altered("${trans}" "${sync_lines}" "${I12}esp_err_t sync_ret = esp_cache_msync((void *)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);\n" t)
make_copy(sync-call "${t}" "${cmd}")
check("the sync in the other direction" "${OUT}/sync-call" FALSE TRUE)
make_copy(sync-none "" "${cmd}")
check("no sd_trans_sdmmc.c" "${OUT}/sync-none" FALSE TRUE)
# the old line live but spelled another way, beside upstream's own lines
altered("${old_trans}" "${sync_old}" "${I12}ret = esp_cache_msync(cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n" t)
make_copy(sync-comment-nocast "/*\n${sync_hunk}*/\n${t}" "${cmd}")
check("the fixed lines only in a comment, the old line below without its cast" "${OUT}/sync-comment-nocast" FALSE TRUE)
altered("${trans}" "${sync_hunk}" "${sync_hunk}${I12}ret = esp_cache_msync((void*)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n" t)
make_copy(sync-after-tight "${t}" "${cmd}")
check("the old line after the fixed lines, written (void*)" "${OUT}/sync-after-tight" FALSE TRUE)
altered("${trans}" "${sync_hunk}" "${sync_hunk}${I12}void *buf = cmdinfo->data;\n${I12}ret = esp_cache_msync(buf, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_UNALIGNED);\n" t)
make_copy(sync-after-other "${t}" "${cmd}")
check("a second sync after the fixed lines, on another name, with another flag" "${OUT}/sync-after-other" FALSE TRUE)

message("== 007945125db7 altered: the CMD25 fix must not count")
altered("${cmd}" "__func__, err_acmd22);\n${I12}}\n" "__func__, err_acmd22);\n${I12}}\n${I12}err = err_acmd22;\n" t)
make_copy(cmd25-after "${trans}" "${t}")
check("the count's result taken as the write's after the fixed lines" "${OUT}/cmd25-after" TRUE FALSE)
altered("${cmd}" "${cmd25_lines}" "${cmd25_old}${cmd25_lines}" t)
make_copy(cmd25-beside "${trans}" "${t}")
check("the old line left beside the new ones" "${OUT}/cmd25-beside" TRUE FALSE)
make_copy(cmd25-comment "${trans}" "/*\n${cmd25_hunk}*/\n${old_cmd}")
check("the fixed lines only in a comment, the old code below" "${OUT}/cmd25-comment" TRUE FALSE)
altered("${cmd}" "if (err_acmd22 == ESP_OK) {" "if (err == ESP_OK) {" t)
make_copy(cmd25-test "${trans}" "${t}")
check("the write's error tested in place of the count's" "${OUT}/cmd25-test" TRUE FALSE)
make_copy(cmd25-none "${trans}" "")
check("no sdmmc_cmd.c" "${OUT}/cmd25-none" TRUE FALSE)
# the old line live but spelled another way, beside upstream's own lines
altered("${cmd}" "${cmd25_hunk}" "${cmd25_hunk}${I12}err = sdmmc_send_cmd_num_of_written_blocks(card,&successfully_written_blocks);\n" t)
make_copy(cmd25-after-tight "${trans}" "${t}")
check("the count into err again after the fixed lines, written (card,&" "${OUT}/cmd25-after-tight" TRUE FALSE)
altered("${cmd}" "${cmd25_hunk}" "${cmd25_hunk}${I12}err = sdmmc_send_cmd_num_of_written_blocks(card, NULL);\n" t)
make_copy(cmd25-after-null "${trans}" "${t}")
check("the count into err again after the fixed lines, with NULL" "${OUT}/cmd25-after-null" TRUE FALSE)
altered("${old_cmd}" "${cmd25_old}" "${I12}err = sdmmc_send_cmd_num_of_written_blocks(card,&successfully_written_blocks);\n" t)
make_copy(cmd25-comment-tight "${trans}" "/*\n${cmd25_hunk}*/\n${t}")
check("the fixed lines only in a comment, the old line below written (card,&" "${OUT}/cmd25-comment-tight" TRUE FALSE)

message("== white space changed only: both fixes must still count")
string(REPLACE "    " "\t" t1 "${trans}")
string(REPLACE "\n" "\r\n" t1 "${t1}")
string(REPLACE "    " "\t" t2 "${cmd}")
string(REPLACE "\n" "\r\n" t2 "${t2}")
make_copy(spaces "${t1}" "${t2}")
check("tabs for spaces, CRLF line ends" "${OUT}/spaces" TRUE TRUE)
string(REPLACE "if (" "if(" t1 "${trans}")
string(REPLACE "(void *)" "(void*)" t1 "${t1}")
string(REPLACE "if (" "if(" t2 "${cmd}")
string(REPLACE "(card, &" "(card,&" t2 "${t2}")
make_copy(tight "${t1}" "${t2}")
check("spaces taken out between tokens: if(, (void*), (card,&" "${OUT}/tight" TRUE TRUE)

message("== the ESP-IDF at ${IDF}")
tiny386_sdmmc_fixes("${IDF}" sync cmd25)
set(seen "")
foreach(c 188e3e55bb sync cmd25 both)
    if(EXISTS "${IDF}/${TRANS}" AND EXISTS "${IDF}/${CMD}")
        file(READ "${IDF}/${TRANS}" a)
        file(READ "${IDF}/${CMD}" b)
        file(READ "${OUT}/${c}/${TRANS}" ca)
        file(READ "${OUT}/${c}/${CMD}" cb)
        if(a STREQUAL ca AND b STREQUAL cb)
            set(seen ${c})
        endif()
    endif()
endforeach()
if(seen STREQUAL "")
    message("  its two files are none of the copies: cache sync fix ${sync}, CMD25 fix ${cmd25}, not checked")
else()
    set(w_sync FALSE)
    set(w_cmd25 FALSE)
    if(seen STREQUAL "sync" OR seen STREQUAL "both")
        set(w_sync TRUE)
    endif()
    if(seen STREQUAL "cmd25" OR seen STREQUAL "both")
        set(w_cmd25 TRUE)
    endif()
    check("the same files as the copy ${seen}" "${IDF}" ${w_sync} ${w_cmd25})
endif()

message("test_sdmmc_fixes: ${cases} cases, ${fails} failed")
if(NOT fails EQUAL 0)
    message(FATAL_ERROR "sdmmc_fixes.cmake got ${fails} of ${cases} wrong")
endif()
