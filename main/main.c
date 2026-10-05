#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/uart.h"

// ESP32-C6: GPIO0 is an ADC-capable pin. Change this if your sensor uses another ADC pin.
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_0
#define ADC_ATTEN           ADC_ATTEN_DB_12
#define SAMPLE_INTERVAL_MS  10

static const char *TAG = "heart_adc";

static bool stop_requested_uart(void)
{
    uint8_t ch = 0;
    int rx = uart_read_bytes(UART_NUM_0, &ch, 1, 0);
    if (rx > 0) {
        if ((ch == 'q') || (ch == 'Q') || (ch == 0x03)) {
            return true;
        }
    }

    return false;
}

float processar_senyal_pols(uint16_t senyal_bruta)
{
    const float ALPHA_LPF = 0.0363f; // fc = 0.6 Hz
    const float BETA_HPF = 0.8f; // fc = 4 Hz

    static float lpf_prev = 2048.0f;
    static float hpf_prev = 0.0f;
    static float lpf_prev_prev = 2048.0f;

    float input_raw = (float)senyal_bruta;

    float lpf_out = (ALPHA_LPF * input_raw) + ((1.0f - ALPHA_LPF) * lpf_prev);
    lpf_prev = lpf_out;

    float hpf_out = BETA_HPF * (hpf_prev + lpf_out - lpf_prev_prev);
    hpf_prev = hpf_out;
    lpf_prev_prev = lpf_out;

    return hpf_out;
}

void app_main(void)
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
    };

    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };

    err = adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(adc_handle);
        return;
    }

    err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Starting ADC sampling every %d ms", SAMPLE_INTERVAL_MS);
    ESP_LOGI(TAG, "Output format: DATA,<ms>,<raw>,<filtered>");
    ESP_LOGI(TAG, "Press 'q' to stop sampling");

    bool running = true;
    while (running) {
        if (stop_requested_uart()) {
            running = false;
            continue;
        }

        int raw = 0;
        err = adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
        if (err == ESP_OK) {
            float filtrat = processar_senyal_pols((uint16_t)raw);
            int64_t now_ms = esp_timer_get_time() / 1000;
            printf("DATA,%lld,%d,%.2f\n", now_ms, raw, filtrat);
        } else {
            ESP_LOGW(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Stopping ADC sampling");
    adc_oneshot_del_unit(adc_handle);
}
