#ifndef VOLUMETRICMEASUREMENTSOURCE_H
#define VOLUMETRICMEASUREMENTSOURCE_H

// PRO-4: the volumetric-measurement source a shot is currently consuming.
//
// Extracted from Controller.h into this standalone, dependency-free header so
// the pure source-selection logic (VolumetricSourcePolicy.h) can be host-tested
// in [env:native] without linking Controller/BLE/LVGL/FreeRTOS.
//
//   - INACTIVE:        no shot in progress (or no volumetric target).
//   - FLOW_ESTIMATION: pump/flow-model estimate from the controller sensor
//                      stream (independent of the BLE scale).
//   - BLUETOOTH:       weight from the connected BLE scale.
enum class VolumetricMeasurementSource { INACTIVE, FLOW_ESTIMATION, BLUETOOTH };

#endif // VOLUMETRICMEASUREMENTSOURCE_H
