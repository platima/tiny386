#pragma once
static inline BaseType_t xTaskCreatePinnedToCore(void (*f)(void *), const char *n, int s, void *a, int p, TaskHandle_t *h, int c) { (void) f; (void) n; (void) s; (void) a; (void) p; (void) h; (void) c; return pdPASS; }
static inline void vTaskDelay(int t) { (void) t; }
static inline void vTaskDelete(TaskHandle_t t) { (void) t; }
