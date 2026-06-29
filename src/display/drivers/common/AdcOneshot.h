/**
 * @file      AdcOneshot.h
 * @brief     ESP-IDF 5.x ADC oneshot + calibration helper for panel battery reads.
 *
 * PRO-329: the panel drivers historically used the deprecated legacy ADC API
 * (esp_adc_cal_characterize / analogRead). On Arduino-esp32 3.x (PRO-293 platform
 * migration) the legacy ADC driver is forbidden once the new driver_ng (oneshot)
 * ADC is active — linking esp_adc_cal_legacy.c trips the conflict guard
 * (check_adc_oneshot_driver_conflict) and abort()s during static init, boot-looping
 * the device. This helper reuses ONLY the new oneshot + curve-fitting calibration
 * API so no legacy symbol is linked and the conflict guard never fires.
 *
 * Voltage semantics are preserved from the legacy code: read raw at ADC_ATTEN_DB_12 /
 * 12-bit, convert to mV via calibration, multiply by 2 for the on-board divider.
 */
#pragma once

#include <cstdint>

/**
 * Read a battery/detect voltage divider on the given ADC-capable GPIO.
 *
 * Sets up an ADC1 oneshot unit + curve-fitting calibration for the channel that
 * maps to @p gpio, averages @p samples raw reads (2 ms apart), converts to
 * millivolts, and multiplies by 2 to undo the resistor divider. All driver
 * handles are created and torn down within the call, so nothing persists to
 * conflict with the rest of the system's ADC usage.
 *
 * @param gpio     ADC1-capable GPIO (e.g. GPIO4 on ESP32-S3 = ADC1_CH3).
 * @param samples  Number of raw samples to average (legacy used 20).
 * @return         Battery voltage in millivolts (already x2 for the divider),
 *                 or 0 if the GPIO is not ADC-capable / setup failed.
 */
uint16_t adcOneshotReadBattMillivolts(int gpio, int samples = 20);
