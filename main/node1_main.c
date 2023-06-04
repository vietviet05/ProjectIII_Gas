/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>  
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h" 
#include "sdkconfig.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "soc/rtc.h"
#include "esp_task_wdt.h"

const static char *TAG = "EXAMPLE";

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
//ADC1 Channels
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_4
#define EXAMPLE_ADC1_CHAN1          ADC_CHANNEL_5
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_11


static int adc_raw[2];
static int voltage[2]; 
double ppm;
double y, Rs;
static bool example_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);
bool isGasNormal = true; 


   //Hàm cài đặt/thay đổi thời gian ngủ sâu
int sleep_per[] = {5, 10 , 15};
int sleep_per_count = sizeof(sleep_per) / sizeof(sleep_per[0]);
RTC_DATA_ATTR int current_sleep_per_index = 0;
void button_press_event_handler() { 
   
    // In giá trị mới của wakeup_time_sec
    printf("thoi gian ngu la:  %ds\n",sleep_per[current_sleep_per_index]);
    // Enable timer wakeup với giá trị mới của wakeup_time_sec
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_per[current_sleep_per_index] * 1000000));
    // Tăng chỉ số của mảng sleep_per hoặc đặt lại về 0 nếu đã đạt đến cuối mảng
    current_sleep_per_index = (current_sleep_per_index + 1) % sleep_per_count;
} 
void app_main(void) 
{    
        //-------------ADC1 Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));
    

    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_handle = NULL;
    bool do_calibration1 = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC_ATTEN, &adc1_cali_handle);
   
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT0: {
            printf("Wake up from ext0\n"); 
            button_press_event_handler();
    
            break;
        }
        default:
        // Xử lý trường hợp mặc định
        break;
     }
// Cấu hình ngắt nút nhấn RTC   
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_per[0] * 1000000)); // Chu kỳ mặc định là 180s
    const int ext_wakeup_pin_0 = 0;
  
    printf("Enabling EXT0 wakeup on pin GPIO%d\n", ext_wakeup_pin_0);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(ext_wakeup_pin_0, 0));

    // Configure pullup/downs via RTCIO to tie wakeup pins to inactive level during deepsleep.
    // EXT0 resides in the same power domain (RTC_PERIPH) as the RTC IO pullup/downs.
    // No need to keep that power domain explicitly, unlike EXT1.
    ESP_ERROR_CHECK(rtc_gpio_pullup_dis(ext_wakeup_pin_0));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_en(ext_wakeup_pin_0));   
    
    
    
 //Cấu hình GPIO 
     gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set 2
    io_conf.pin_bit_mask = (1ULL << 18);
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);   


       

 while (1){   

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[0]));
    ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw[0]);
    if (do_calibration1) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw[0], &voltage[0]));
        ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, voltage[0]);
    }
        Rs = (5000.10000 / voltage[0]) - 10000;
        y = Rs/439; 
        ppm = pow(10, (log(y) - 1.413)/(-0.473)); 
        ESP_LOGI(TAG, "Nong do khi gas: %f", ppm);
        vTaskDelay(pdMS_TO_TICKS(1000));
    if (adc_raw[0] > 100) {
        printf("Warning: Gas level exceeded threshold!\n");
        gpio_set_level (18, 1);   
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    else {
        printf("Gas level is back to normal.\n");
        break; // Thoát khỏi vòng lặp khi khí gas trở lại bình thường
        }
    }
   vTaskDelay(pdMS_TO_TICKS(1000));
// Sau khi khí gas trở lại bình thường, tiến hành ngủ sâu
esp_deep_sleep_start();



    //Tear Down
ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
if (do_calibration1) {
example_adc_calibration_deinit(adc1_cali_handle);
    }

}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;


// ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
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

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
