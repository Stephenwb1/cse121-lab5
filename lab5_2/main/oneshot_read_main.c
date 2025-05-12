/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"

const static char *TAG = "EXAMPLE";

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
// ADC1 Channels
#if CONFIG_IDF_TARGET_ESP32
#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_4
#define EXAMPLE_ADC1_CHAN1 ADC_CHANNEL_5
#else
#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_2
#define EXAMPLE_ADC1_CHAN1 ADC_CHANNEL_3
#endif

#if (SOC_ADC_PERIPH_NUM >= 2) && !CONFIG_IDF_TARGET_ESP32C3
/**
 * On ESP32C3, ADC2 is no longer supported, due to its HW limitation.
 * Search for errata on espressif website for more details.
 */
#define EXAMPLE_USE_ADC2 1
#endif

#if EXAMPLE_USE_ADC2
// ADC2 Channels
#if CONFIG_IDF_TARGET_ESP32
#define EXAMPLE_ADC2_CHAN0 ADC_CHANNEL_0
#else
#define EXAMPLE_ADC2_CHAN0 ADC_CHANNEL_0
#endif
#endif // #if EXAMPLE_USE_ADC2

#define EXAMPLE_ADC_ATTEN ADC_ATTEN_DB_12

static int adc_raw[2][10];
static int voltage[2][10];

#define UNIT_MICRO 200000
#define UNIT UNIT_MICRO / 1000
#define DOT_DURATION_MS      1 * UNIT
#define SYMBOL_DURATION_MS   2 * UNIT
#define DASH_DURATION_MS     3 * UNIT
#define LETTER_GAP_MS        4 * UNIT
#define WORD_GAP_MS          7 * UNIT

#define THRESHOLD_MV         110  // Adjust this experimentally

typedef struct {
    const char *morse;
    char letter;
} morse_map_t;

const morse_map_t morse_table[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'}, {".", 'E'},
    {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'}, {"..", 'I'}, {".---", 'J'},
    {"-.-", 'K'}, {".-..", 'L'}, {"--", 'M'}, {"-.", 'N'}, {"---", 'O'},
    {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'}, {"-.--", 'Y'},
    {"--..", 'Z'}, {"", ' '}  // Word gap as empty string
};

char morse_to_char(const char *code) {
    for (int i = 0; i < sizeof(morse_table) / sizeof(morse_table[0]); i++) {
        if (strcmp(code, morse_table[i].morse) == 0)
            return morse_table[i].letter;
    }
    return '?';  // Unknown
}


char print_morse(adc_oneshot_unit_handle_t adc_handle, adc_cali_handle_t cali_handle) {

    char morse_code[10] = {0};
    int index = 0;
    int last_state = 0;
    int64_t last_change = esp_timer_get_time() / 1000;

    while (1) {
        int adc_raw, voltage;
        adc_oneshot_read(adc_handle, ADC_CHANNEL_2, &adc_raw);
        adc_cali_raw_to_voltage(cali_handle, adc_raw, &voltage);
        int current_state = (voltage > THRESHOLD_MV) ? 1 : 0;

        int64_t now = esp_timer_get_time() / 1000;
        int duration = now - last_change;

        if (current_state != last_state) {
            // ESP_LOGI(TAG, "CURRENT STATE: %d", current_state);
            last_change = now;


            if (last_state == 1) {
                // LED just turned off – mark dot or dash
                if (duration < DASH_DURATION_MS) {
                    ESP_LOGI(TAG, "DOT DETECTED");
                    morse_code[index++] = '.';
                } else {
                    ESP_LOGI(TAG, "DASH DETECTED");
                    morse_code[index++] = '-';
                }
            }
            last_state = current_state;
        }

        // If light has been OFF long enough, decode and print
        // ESP_LOGI(TAG, "duration = %d", duration);
        if (current_state == 0 && duration > LETTER_GAP_MS && index > 0) {
            ESP_LOGI(TAG, "DECODING...");
            morse_code[index] = '\0';
            char letter = morse_to_char(morse_code);
            ESP_LOGI(TAG, "%c", letter);
            fflush(stdout);
            return (letter);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                         adc_atten_t atten,
                                         adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

void app_main(void) {
  //-------------ADC1 Init---------------//
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

  //-------------ADC1 Config---------------//
  adc_oneshot_chan_cfg_t config = {
      .atten = EXAMPLE_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN1, &config));

  //-------------ADC1 Calibration Init---------------//
  adc_cali_handle_t adc1_cali_chan0_handle = NULL;
  adc_cali_handle_t adc1_cali_chan1_handle = NULL;
  bool do_calibration1_chan0 =
      example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0,
                                   EXAMPLE_ADC_ATTEN, &adc1_cali_chan0_handle);
  bool do_calibration1_chan1 =
      example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN1,
                                   EXAMPLE_ADC_ATTEN, &adc1_cali_chan1_handle);
  char *letter_index = malloc(64);
  if (letter_index == NULL) {
    printf("Memory allocation failure\n");
  }
  for (int i = 0; i < 64; i++) {
    letter_index[i] = '\0';
  }
  int i = 0;
  while (1) {
    ESP_ERROR_CHECK(
        adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[0][0]));
    // if (do_calibration1_chan0) {
    //   ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle,
    //                                           adc_raw[0][0], &voltage[0][0]));
    //   ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1,
    //            EXAMPLE_ADC1_CHAN0, voltage[0][0]);
    //   if (voltage[0][0] > THRESHOLD_MV) {
    //     ESP_LOGI(TAG, "LIGHT HAS FLASHED\n");
    //   }
    // }
    char letter = print_morse(adc1_handle, adc1_cali_chan0_handle);
    letter_index[i] = letter;
    i++;
    ESP_LOGI(TAG, "WORD IS %s", letter_index);
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Tear Down
  ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
  if (do_calibration1_chan0) {
    example_adc_calibration_deinit(adc1_cali_chan0_handle);
  }
  if (do_calibration1_chan1) {
    example_adc_calibration_deinit(adc1_cali_chan1_handle);
  }

}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                         adc_atten_t atten,
                                         adc_cali_handle_t *out_handle) {
  adc_cali_handle_t handle = NULL;
  esp_err_t ret = ESP_FAIL;
  bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
      calibrated = true;
    }
  }
#endif

  *out_handle = handle;
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Calibration Success");
  } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
    ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
  } else {
    ESP_LOGE(TAG, "Invalid arg or no memory");
  }

  return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
  ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
  ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
