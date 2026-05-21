#include "main.h"
#include "ControllerConfig.h"
#include "GaggiMateController.h"

GaggiMateController controller(BUILD_GIT_VERSION);

void setup() {
    Serial.begin(115200);
    controller.setup();
}

void loop() {
    if (Serial.available()) {
        int c = Serial.read();
        if (c == 'B') {
            ESP_LOGW("MAIN", "Serial trigger: factory-reset BLE bonds");
            controller.factoryResetBonds();
        }
    }
    controller.loop();
}
