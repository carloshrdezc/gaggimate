#include "main.h"

#include <esp_log.h>

#ifndef GAGGIMATE_HEADLESS
#include <lvgl.h>
#endif

Controller controller;

void setup() {
    Serial.begin(115200);
    // PRO-273: with the ESP_LOG* -> esp_log_write() tee shim (src/display/EspLogTee.h),
    // the firmware's ESP_LOG calls now flow through the IDF logging path so the
    // DiagnosticLogPlugin tee can capture them. initArduino() defaults the runtime
    // tag level to ERROR (esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL=1)), which
    // would otherwise drop every INFO/WARN line before it reaches UART OR the tee —
    // a regression vs the old log_printf() path that printed everything
    // CORE_DEBUG_LEVEL=3 allowed. Raise the runtime level back to INFO so INFO+ keeps
    // printing to UART exactly as before and is available to the tee. (The shim's
    // compile-time gate is also INFO; DEBUG/VERBOSE stay compiled out.)
    esp_log_level_set("*", ESP_LOG_INFO);
    controller.setup();
}

void loop() {
    controller.loop();
    delay(2); // Yield to FreeRTOS idle task to prevent watchdog starvation
}
