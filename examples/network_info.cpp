#include "bc660k.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "NETINFO_EXAMPLE";

bc660k modem;

extern "C" void app_main()
{
    modem.init(GPIO_NUM_17, GPIO_NUM_16, UART_NUM_1, 9600);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    modem.set_apn("IP", "your_apn_here", "", "");
    modem.set_nwscanmode(3);
    modem.wait_network_registered(60000);

    char op[32];
    if (modem.get_operator(op))
        ESP_LOGI(TAG, "Operator: %s", op);

    int rssi;
    if (modem.get_rssi(&rssi))
        ESP_LOGI(TAG, "RSSI: %d dBm", rssi);

    char nwinfo[128];
    if (modem.get_nwinfo(nwinfo))
        ESP_LOGI(TAG, "NWINFO: %s", nwinfo);

    char cell[200];
    if (modem.get_serving_cell(cell))
        ESP_LOGI(TAG, "Serving Cell: %s", cell);
}