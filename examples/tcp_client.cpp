#include "bc660k.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "TCP_EXAMPLE";

bc660k modem;

extern "C" void app_main()
{
    modem.init(GPIO_NUM_17, GPIO_NUM_16, UART_NUM_1, 9600);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    modem.set_apn("IP", "your_apn_here", "", "");
    modem.set_nwscanmode(3);
    modem.wait_network_registered(60000);
    modem.activate_pdp(1);

    ESP_LOGI(TAG, "Opening TCP socket...");
    if (!modem.socket_open(0, "TCP", "example.com", 80)) {
        ESP_LOGE(TAG, "Socket open failed");
        return;
    }

    const char* http_req =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n\r\n";

    ESP_LOGI(TAG, "Sending HTTP request...");
    modem.socket_send(0, http_req, strlen(http_req));

    char buffer[512];
    ESP_LOGI(TAG, "Receiving data...");
    if (modem.socket_receive(0, buffer, sizeof(buffer)-1, 5000)) {
        ESP_LOGI(TAG, "Received:\n%s", buffer);
    }

    modem.socket_close(0);
}