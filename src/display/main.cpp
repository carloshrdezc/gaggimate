#include "main.h"

#ifndef GAGGIMATE_HEADLESS
#include <lvgl.h>
#endif

Controller controller;

void setup() {
    Serial.begin(115200);
    controller.setup();
}

void loop() {
    if (Serial.available()) {
        int c = Serial.read();
        if (c == 'B') {
            ESP_LOGW("MAIN", "Serial trigger: factory-reset BLE bonds");
            controller.getClientController()->factoryResetBonds();
        }
    }
    controller.loop();
    delay(2);  // Yield to FreeRTOS idle task to prevent watchdog starvation
}
