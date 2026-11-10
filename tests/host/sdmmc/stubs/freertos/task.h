/* host stand-in: the task sending the command is the one the test says */
#pragma once
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
