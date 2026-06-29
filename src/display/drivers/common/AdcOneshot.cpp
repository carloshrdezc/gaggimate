/**
 * @file      AdcOneshot.cpp
 * @brief     ESP-IDF 5.x ADC oneshot + calibration helper. See AdcOneshot.h (PRO-329).
 */
#include "AdcOneshot.h"

#include <Arduino.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

static const char *ADC_ONESHOT_TAG = "AdcOneshot";

uint16_t adcOneshotReadBattMillivolts(int gpio, int samples) {
    if (samples <= 0) {
        return 0;
    }

    // Map the GPIO to its ADC unit + channel. Bails out cleanly for non-ADC pins.
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    if (adc_oneshot_io_to_channel(gpio, &unit, &channel) != ESP_OK) {
        ESP_LOGW(ADC_ONESHOT_TAG, "GPIO %d is not ADC-capable", gpio);
        return 0;
    }

    adc_oneshot_unit_handle_t adc_handle = nullptr;
    const adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_cfg, &adc_handle) != ESP_OK) {
        ESP_LOGW(ADC_ONESHOT_TAG, "adc_oneshot_new_unit failed");
        return 0;
    }

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(adc_handle, channel, &chan_cfg) != ESP_OK) {
        ESP_LOGW(ADC_ONESHOT_TAG, "adc_oneshot_config_channel failed");
        adc_oneshot_del_unit(adc_handle);
        return 0;
    }

    // Average the requested number of raw samples (matches the legacy 20-sample loop).
    uint32_t sum = 0;
    int valid = 0;
    for (int i = 0; i < samples; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc_handle, channel, &raw) == ESP_OK) {
            sum += static_cast<uint32_t>(raw);
            valid++;
        }
        delay(2);
    }
    if (valid == 0) {
        adc_oneshot_del_unit(adc_handle);
        return 0;
    }
    const int raw_avg = static_cast<int>(sum / static_cast<uint32_t>(valid));

    // Curve-fitting calibration (the scheme supported on ESP32-S3) to convert raw -> mV.
    adc_cali_handle_t cali_handle = nullptr;
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    uint16_t millivolts = 0;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
        int voltage = 0;
        if (adc_cali_raw_to_voltage(cali_handle, raw_avg, &voltage) == ESP_OK) {
            // x2 undoes the on-board resistor divider (preserved from legacy code).
            millivolts = static_cast<uint16_t>(voltage * 2);
        }
        adc_cali_delete_scheme_curve_fitting(cali_handle);
    } else {
        ESP_LOGW(ADC_ONESHOT_TAG, "adc_cali_create_scheme_curve_fitting failed");
    }

    adc_oneshot_del_unit(adc_handle);
    return millivolts;
}
