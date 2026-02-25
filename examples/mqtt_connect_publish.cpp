#include "bc660k.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "MQTT_EXAMPLE";

bc660k modem;

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing modem...");
    modem.init(GPIO_NUM_17, GPIO_NUM_16, UART_NUM_1, 9600);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Configuring APN...");
    if (!modem.set_apn("IP", "your_apn_here", "", "")) {
        ESP_LOGE(TAG, "APN configuration failed");
        return;
    }

    modem.set_nwscanmode(3);
    modem.set_iotopmode(1);
    modem.set_band(1);
    modem.enable_network_registration();

    ESP_LOGI(TAG, "Waiting for network registration...");
    if (!modem.wait_network_registered(60000)) {
        ESP_LOGE(TAG, "Network registration failed");
        return;
    }

    ESP_LOGI(TAG, "Activating PDP...");
    if (!modem.activate_pdp(1)) {
        ESP_LOGE(TAG, "PDP activation failed");
        return;
    }

    ESP_LOGI(TAG, "Opening MQTT connection...");
    if (!modem.mqtt_open("test.mosquitto.org", 1883)) {
        ESP_LOGE(TAG, "MQTT open failed");
        return;
    }

    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    if (!modem.mqtt_connect("BC660K_CLIENT", "", "")) {
        ESP_LOGE(TAG, "MQTT connect failed");
        return;
    }

    ESP_LOGI(TAG, "Publishing message...");
    if (!modem.mqtt_publish("bc660k/test", "Hello from BC660K!", 0)) {
        ESP_LOGE(TAG, "Publish failed");
        return;
    }

    ESP_LOGI(TAG, "Message published successfully!");

    while (true) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}