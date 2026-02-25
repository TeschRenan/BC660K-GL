#include "bc660k.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MQTT_EXAMPLE";

bc660k modem;

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing modem...");
    modem.init(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_12, UART_NUM_1, 115200);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    char ip[32];
    if (modem.wait_for_ip(180000, ip))
    {
        ESP_LOGI(TAG, "Modem IP: %s", ip);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to obtain IP address");

        ESP_LOGE(TAG, "Failed to connect to network");

        ESP_LOGI(TAG, "Reading ICCID...");
        char iccid[64];
        modem.get_iccid(iccid);

        ESP_LOGI(TAG, "ICCID: %s", iccid);

        ESP_LOGI(TAG, "Configuring APN...");
        modem.set_apn("IP", "lf.br", "", "");

        ESP_LOGI(TAG, "Setting band...");
        modem.set_band(1);

        ESP_LOGI(TAG, "Enabling network registration...");
        modem.enable_network_registration();

        ESP_LOGI(TAG, "Waiting for network registration...");
        modem.enable_connection();

        modem.set_operator(4, 2, "72411");

        ESP_LOGI(TAG, "Waiting for connection...");

        if (!modem.wait_for_ip(180000, ip))
        {
            ESP_LOGE(TAG, "Failed to connect to network");
            return;
        }
    }

    ESP_LOGI(TAG, "Connected to network!");
    char utc[64];
    ESP_LOGI(TAG, "Getting network time...");
    modem.get_time(utc);

    ESP_LOGI(TAG, "UTC Time: %s", utc);
    int rssi = 0;
    modem.get_rssi(&rssi);

    ESP_LOGI(TAG, "Signal strength (RSSI): %d dBm", rssi);

    ESP_LOGI(TAG, "Ensuring RRC connected...");

    if (!modem.wait_rrc_connected(10000))
    {
        ESP_LOGE(TAG, "Modem did not enter RRC Connected state");
        return;
    }

    modem.mqtt_configure();

    ESP_LOGI(TAG, "Opening MQTT connection...");

    if (modem.mqtt_is_open("test.mosquitto.org", 1883))
    {
        ESP_LOGI(TAG, "MQTT already open");
    }
    else if (!modem.mqtt_open("test.mosquitto.org", 1883))
    {
        ESP_LOGE(TAG, "MQTT open failed");
        return;
    }
    else
    {
        ESP_LOGI(TAG, "MQTT connection opened successfully");

        ESP_LOGI(TAG, "Connecting to MQTT broker...");
        if (!modem.mqtt_connect("BC660K_CLIENT", "", ""))
        {
            ESP_LOGE(TAG, "MQTT connect failed");
            return;
        }
    }

    ESP_LOGI(TAG, "Publishing message...");
    if (!modem.mqtt_publish("bc660k/test", "Hello from BC660K!", 0))
    {
        ESP_LOGE(TAG, "Publish failed");
        return;
    }

    ESP_LOGI(TAG, "Message published successfully!");

    while (true)
    {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}