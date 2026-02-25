#include "bc660k.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "UDP_EXAMPLE";

bc660k modem;

extern "C" void app_main()
{
    modem.init(GPIO_NUM_17, GPIO_NUM_16, UART_NUM_1, 9600);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    modem.set_apn("IP", "your_apn_here", "", "");
    modem.set_nwscanmode(3);
    modem.wait_network_registered(60000);
    modem.activate_pdp(1);

    ESP_LOGI(TAG, "Opening UDP socket...");
    if (!modem.socket_open(0, "UDP", "echo.u-blox.com", 7)) {
        ESP_LOGE(TAG, "UDP open failed");
        return;
    }

    const char* msg = "Hello via UDP!";
    modem.socket_send(0, msg, strlen(msg));

    char buffer[256];
    if (modem.socket_receive(0, buffer, sizeof(buffer)-1, 5000)) {
        ESP_LOGI(TAG, "Echo received: %s", buffer);
    }

    modem.socket_close(0);
}