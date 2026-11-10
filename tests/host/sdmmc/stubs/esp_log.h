/* host stand-in: the log lines counted and printed */
#pragma once
#include <stdio.h>
extern int host_log_lines;
#define ESP_LOGE(tag, fmt, ...) \
	(host_log_lines++, printf("    | E %s: " fmt "\n", tag, ##__VA_ARGS__))
