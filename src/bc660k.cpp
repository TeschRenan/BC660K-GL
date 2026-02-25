/**
	@file bc660k.cpp
	@author Renan Tesch

	MIT License

	Copyright (c) Copyright 2026 Renan Tesch
	GitHub https://github.com/TeschRenan

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#include "bc660k.hpp"

/**
 * @brief Initialize UART hardware layer.
 * @param u Pointer to UART layer structure.
 * @param tx TX GPIO pin.
 * @param rx RX GPIO pin.
 * @param port UART port number.
 * @param baud_rate UART baud rate.
 * @return void
 */
static void uart_layer_init(uart_layer_t* u,
                            gpio_num_t tx,
                            gpio_num_t rx,
                            uart_port_t port,
                            int baud_rate)
{
    u->port = port;
    u->default_timeout_ms = 100;

    gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    gpio_set_direction(rx, GPIO_MODE_INPUT);
    gpio_set_pull_mode(rx, GPIO_PULLUP_ONLY);

    uart_config_t cfg = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    uart_param_config(port, &cfg);
    uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(port, 2048, 1024, 0, NULL, 0);
}

/**
 * @brief Write raw bytes to UART.
 * @param u Pointer to UART layer structure.
 * @param data Pointer to data buffer.
 * @param len Number of bytes to send.
 * @return Number of bytes written.
 * @return 0 If data is null or len <= 0.
 */
static int uart_layer_write(uart_layer_t* u, const char* data, int len)
{
    if (!data || len <= 0) return 0;
    return uart_write_bytes(u->port, data, len);
}

/**
 * @brief Read raw bytes from UART.
 * @param u Pointer to UART layer structure.
 * @param buf Output buffer.
 * @param max_len Maximum bytes to read.
 * @param timeout_ms Timeout in milliseconds.
 * @return Number of bytes read.
 * @return 0 If buffer is null or max_len <= 0.
 */
static int uart_layer_read(uart_layer_t* u, char* buf, int max_len, int timeout_ms)
{
    if (!buf || max_len <= 0) return 0;
    return uart_read_bytes(u->port, (uint8_t*)buf, max_len, timeout_ms / portTICK_PERIOD_MS);
}

/**
 * @brief Flush UART RX buffer.
 * @param u Pointer to UART layer structure.
 * @return void
 */
static void uart_layer_flush(uart_layer_t* u)
{
    uart_flush(u->port);
}

/**
 * @brief Initialize AT command layer.
 * @param at Pointer to AT layer structure.
 * @param uart Pointer to UART layer structure.
 * @return void
 */
static void at_layer_init(at_layer_t* at, uart_layer_t* uart)
{
    at->uart = uart;
    memset(at->line_buf, 0, sizeof(at->line_buf));
}

/**
 * @brief Send AT command string.
 * @param at Pointer to AT layer structure.
 * @param cmd Null-terminated AT command string.
 * @return true Command sent successfully.
 * @return false Invalid command or zero length.
 */
static bool at_send(at_layer_t* at, const char* cmd)
{
    if (!cmd) return false;
    int len = strlen(cmd);
    if (len <= 0) return false;

    uart_layer_flush(at->uart);
    uart_layer_write(at->uart, cmd, len);
    return true;
}

/**
 * @brief Read a full line from UART until '\n'.
 * @param at Pointer to AT layer structure.
 * @param out Output buffer for line.
 * @param max_len Maximum buffer size.
 * @param timeout_ms Timeout in milliseconds.
 * @return true Line read successfully.
 * @return false Timeout or no complete line.
 */
static bool at_read_line(at_layer_t* at, char* out, int max_len, int timeout_ms)
{
    int idx = 0;
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        char c;
        int r = uart_layer_read(at->uart, &c, 1, 10);

        if (r == 1) {
            if (c == '\n') {
                out[idx] = '\0';
                return true;
            }

            if (c != '\r' && idx < max_len - 1) {
                out[idx++] = c;
            }
        }

        elapsed += 10;
    }

    return false;
}

/**
 * @brief Wait for "OK" or "ERROR" response.
 * @param at Pointer to AT layer structure.
 * @param timeout_ms Timeout in milliseconds.
 * @return true If "OK" received.
 * @return false If "ERROR" or timeout.
 */
static bool at_expect_ok(at_layer_t* at, int timeout_ms)
{
    char line[256];

    while (at_read_line(at, line, sizeof(line), timeout_ms)) {
        if (strcmp(line, "OK") == 0)
            return true;

        if (strcmp(line, "ERROR") == 0)
            return false;
    }

    return false;
}

/**
 * @brief Wait for a response starting with a specific prefix.
 * @param at Pointer to AT layer structure.
 * @param prefix Expected prefix string.
 * @param out Output buffer for matched line.
 * @param max_len Maximum buffer size.
 * @param timeout_ms Timeout in milliseconds.
 * @return true Prefix matched successfully.
 * @return false Timeout or "ERROR" received.
 */
static bool at_expect_prefix(at_layer_t* at,
                             const char* prefix,
                             char* out,
                             int max_len,
                             int timeout_ms)
{
    char line[256];

    while (at_read_line(at, line, sizeof(line), timeout_ms)) {
        if (strncmp(line, prefix, strlen(prefix)) == 0) {
            strncpy(out, line, max_len);
            return true;
        }

        if (strcmp(line, "ERROR") == 0)
            return false;
    }

    return false;
}

/**
 * @brief Default constructor for BC660K driver.
 * @return void
 */
bc660k::bc660k()
{
    // Initialize internal state
    mqtt_connected = false;
    mqtt_socket_open = false;
    mqtt_msg_id = 1;

    // Clear UART and AT structures
    memset(&uart, 0, sizeof(uart));
    memset(&at, 0, sizeof(at));
}

/**
 * @brief Initialize BC660K modem driver and UART interface.
 * @param tx GPIO pin used as UART TX.
 * @param rx GPIO pin used as UART RX.
 * @param port UART port number.
 * @param baud_rate UART baud rate.
 * @return true Initialization completed successfully.
 * @return false Invalid parameters or UART initialization failed.
 */
bool bc660k::init(gpio_num_t tx,
                  gpio_num_t rx,
                  uart_port_t port,
                  int baud_rate)
{
    // Store UART configuration
    uart.port = port;
    uart.default_timeout_ms = 100;

    // Configure UART pins
    gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    gpio_set_direction(rx, GPIO_MODE_INPUT);
    gpio_set_pull_mode(rx, GPIO_PULLUP_ONLY);

    // UART configuration
    uart_config_t cfg = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    if (uart_param_config(port, &cfg) != ESP_OK)
        return false;

    if (uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return false;

    if (uart_driver_install(port, 2048, 1024, 0, NULL, 0) != ESP_OK)
        return false;

    // Initialize AT layer
    at.uart = &uart;
    memset(at.line_buf, 0, sizeof(at.line_buf));

    // Flush UART
    uart_flush(port);

    return true;
}

/**
 * @brief Configure APN parameters.
 * @param pdp PDP type (e.g., "IP").
 * @param apn APN name.
 * @param user APN username.
 * @param pass APN password.
 * @return true APN configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_apn(const char* pdp,
                     const char* apn,
                     const char* user,
                     const char* pass)
{
    char cmd[128];

    if (strlen(user) > 0 && strlen(pass) > 0)
        snprintf(cmd, sizeof(cmd), "AT+QCGDEFCONT=\"%s\",\"%s\",\"%s\",\"%s\"\r\n",
                 pdp, apn, user, pass);
    else
        snprintf(cmd, sizeof(cmd), "AT+QCGDEFCONT=\"%s\",\"%s\"\r\n",
                 pdp, apn);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Configure NB-IoT band.
 * @param band 0 = all bands, 1 = bands 3 and 28.
 * @return true Band configured successfully.
 * @return false Invalid band or modem returned ERROR.
 */
bool bc660k::set_band(int band)
{
    char cmd[64];

    if (band == 0)
        snprintf(cmd, sizeof(cmd), "AT+QBAND=0\r\n");
    else if (band == 1)
        snprintf(cmd, sizeof(cmd), "AT+QBAND=2,3,28\r\n");
    else
        return false;

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Configure operator selection.
 * @param mode 0 = automatic, 1 = manual.
 * @param format 0 long alphanumeric, 1 short, 2 numeric.
 * @param oper Operator MCC/MNC when mode != 0.
 * @return true Operator configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_operator(int mode, int format, const char* oper)
{
    char cmd[64];

    if (mode == 0)
        snprintf(cmd, sizeof(cmd), "AT+COPS=0\r\n");
    else
        snprintf(cmd, sizeof(cmd), "AT+COPS=%d,%d,\"%s\"\r\n", mode, format, oper);

    at_send(&at, cmd);
    return at_expect_ok(&at, 5000);
}

/**
 * @brief Enable network registration URC notifications.
 * @return true URC enabled successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::enable_network_registration()
{
    at_send(&at, "AT+CEREG=1\r\n");
    return at_expect_ok(&at, 2000);
}

/**
 * @brief Wait until the modem is registered on the network.
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return true Modem registered (stat = 1 or 5).
 * @return false Timeout or invalid registration state.
 */
bool bc660k::wait_network_registered(int timeout_ms)
{
    char line[256];
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        at_send(&at, "AT+CEREG?\r\n");

        if (at_expect_prefix(&at, "+CEREG:", line, sizeof(line), 2000)) {
            int n, stat;
            sscanf(line, "+CEREG: %d,%d", &n, &stat);

            if (stat == 1 || stat == 5)
                return true;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        elapsed += 1000;
    }

    return false;
}

/**
 * @brief Get current operator MCC/MNC.
 * @param out_mccmnc Output buffer for operator string.
 * @return true Operator retrieved successfully.
 * @return false Timeout or parsing error.
 */
bool bc660k::get_operator(char* out_mccmnc)
{
    char line[256];

    at_send(&at, "AT+COPS?\r\n");

    if (!at_expect_prefix(&at, "+COPS:", line, sizeof(line), 2000))
        return false;

    char* p = strchr(line, '"');
    if (!p) return false;

    p++;
    char* end = strchr(p, '"');
    if (!end) return false;

    *end = '\0';
    strncpy(out_mccmnc, p, 16);

    return true;
}

/**
 * @brief Get RSSI in dBm.
 * @param rssi_dbm Output pointer for RSSI value.
 * @return true RSSI retrieved successfully.
 * @return false Timeout or invalid RSSI (99).
 */
bool bc660k::get_rssi(int* rssi_dbm)
{
    char line[256];

    at_send(&at, "AT+CSQ\r\n");

    if (!at_expect_prefix(&at, "+CSQ:", line, sizeof(line), 2000))
        return false;

    int rssi_raw = 0;
    sscanf(line, "+CSQ: %d,", &rssi_raw);

    if (rssi_raw == 99)
        return false;

    *rssi_dbm = -113 + (rssi_raw * 2);
    return true;
}

/**
 * @brief Get network date/time.
 * @param out_datetime Output buffer for datetime string.
 * @return true Datetime retrieved successfully.
 * @return false Timeout or parsing error.
 */
bool bc660k::get_time(char* out_datetime)
{
    char line[256];

    at_send(&at, "AT+CCLK?\r\n");

    if (!at_expect_prefix(&at, "+CCLK:", line, sizeof(line), 2000))
        return false;

    char* p = strchr(line, '"');
    if (!p) return false;

    p++;
    char* end = strchr(p, '"');
    if (!end) return false;

    *end = '\0';
    strncpy(out_datetime, p, 32);

    return true;
}

/**
 * @brief Set network scan mode.
 * @param mode 0 = automatic, 3 = NB-IoT only.
 * @return true Scan mode configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_nwscanmode(int mode)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"nwscanmode\",%d\r\n", mode);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Set NB-IoT operating mode.
 * @param mode 0 = CAT-NB1, 1 = CAT-NB2.
 * @return true Mode configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_iotopmode(int mode)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"iotopmode\",%d\r\n", mode);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Enable or disable roaming service.
 * @param enable 0 = disable, 1 = enable.
 * @return true Roaming configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_roaming(int enable)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"roamservice\",%d\r\n", enable);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Set network scan sequence.
 * @param seq Sequence string (e.g., "00" or "030201").
 * @return true Sequence configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_scan_sequence(const char* seq)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"nwscanseq\",%s\r\n", seq);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Configure extended band mask.
 * @param mask Band mask string (e.g., "0,80000,80000").
 * @return true Band mask configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_band_extended(const char* mask)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"band\",%s\r\n", mask);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Get network information (technology, band, operator).
 * @param out Output buffer for QNWINFO response.
 * @return true Information retrieved successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::get_nwinfo(char* out)
{
    char line[256];

    at_send(&at, "AT+QNWINFO\r\n");

    if (!at_expect_prefix(&at, "+QNWINFO:", line, sizeof(line), 2000))
        return false;

    strncpy(out, line, 128);
    return true;
}

/**
 * @brief Get serving cell information.
 * @param out Output buffer for QENG response.
 * @return true Serving cell info retrieved successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::get_serving_cell(char* out)
{
    char line[256];

    at_send(&at, "AT+QENG=\"servingcell\"\r\n");

    if (!at_expect_prefix(&at, "+QENG:", line, sizeof(line), 3000))
        return false;

    strncpy(out, line, 200);
    return true;
}

/**
 * @brief Get advanced signal metrics (RSRP, RSRQ, SNR).
 * @param rsrp Output pointer for RSRP value.
 * @param rsrq Output pointer for RSRQ value.
 * @param snr Output pointer for SNR value.
 * @return true Metrics retrieved successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::get_qcsq(int* rsrp, int* rsrq, int* snr)
{
    char line[256];

    at_send(&at, "AT+QCSQ\r\n");

    if (!at_expect_prefix(&at, "+QCSQ:", line, sizeof(line), 2000))
        return false;

    char mode[16];
    sscanf(line, "+QCSQ: \"%15[^\"]\",%d,%d,%d", mode, rsrp, rsrq, snr);

    return true;
}

/**
 * @brief Attach the modem to the packet-switched network.
 * @return true Attach command accepted and modem returned OK.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::attach()
{
    at_send(&at, "AT+CGATT=1\r\n");
    return at_expect_ok(&at, 5000);
}

/**
 * @brief Check if the modem is attached to the network.
 * @param attached Output pointer: true if attached, false otherwise.
 * @return true Status retrieved successfully.
 * @return false Timeout or parsing error.
 */
bool bc660k::is_attached(bool* attached)
{
    char line[256];

    at_send(&at, "AT+CGATT?\r\n");

    if (!at_expect_prefix(&at, "+CGATT:", line, sizeof(line), 2000))
        return false;

    int state = 0;
    sscanf(line, "+CGATT: %d", &state);

    *attached = (state == 1);
    return true;
}

/**
 * @brief Activate PDP context.
 * @param cid Context ID (usually 1).
 * @return true PDP activated successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::activate_pdp(int cid)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QIACT=%d\r\n", cid);

    at_send(&at, cmd);
    return at_expect_ok(&at, 15000);
}

/**
 * @brief Deactivate PDP context.
 * @param cid Context ID (usually 1).
 * @return true PDP deactivated successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::deactivate_pdp(int cid)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QIDEACT=%d\r\n", cid);

    at_send(&at, cmd);
    return at_expect_ok(&at, 10000);
}

/**
 * @brief Configure Power Saving Mode (PSM) parameters.
 * @param tau Requested periodic TAU value (e.g., "00100001").
 * @param active_time Requested Active Time value (e.g., "00000011").
 * @return true PSM configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_psm(const char* tau, const char* active_time)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QPSMS=1,,,\"%s\",\"%s\"\r\n", tau, active_time);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Configure eDRX parameters.
 * @param mode eDRX mode (e.g., "2" for NB-IoT).
 * @param edrx_value eDRX cycle value (e.g., "0101").
 * @return true eDRX configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_edrx(const char* mode, const char* edrx_value)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CEDRXS=%s,\"%s\"\r\n", mode, edrx_value);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Configure MQTT Will message support.
 * @param enable 0 = disable, 1 = enable.
 * @return true Will configuration accepted.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_config_will(int enable)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QMTCFG=\"will\",0,%d\r\n", enable);

    at_send(&at, cmd);
    return at_expect_ok(&at, 3000);
}

/**
 * @brief Open MQTT TCP connection to broker.
 * @param host Broker hostname or IP address.
 * @param port Broker port number (usually 1883).
 * @return true Connection opened successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_open(const char* host, int port)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"%s\",%d\r\n", host, port);

    at_send(&at, cmd);

    char line[256];
    if (!at_expect_prefix(&at, "+QMTOPEN: 0,0", line, sizeof(line), 10000))
        return false;

    mqtt_socket_open = true;
    return true;
}

/**
 * @brief Connect to MQTT broker with authentication.
 * @param client_id MQTT client identifier.
 * @param user Username for authentication.
 * @param pass Password for authentication.
 * @return true Connected successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_connect(const char* client_id, const char* user, const char* pass)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"\r\n",
             client_id, user, pass);

    at_send(&at, cmd);

    char line[256];
    if (!at_expect_prefix(&at, "+QMTCONN: 0,0,0", line, sizeof(line), 10000))
        return false;

    mqtt_connected = true;
    mqtt_msg_id = 1;
    return true;
}

/**
 * @brief Publish MQTT message.
 * @param topic Topic to publish to.
 * @param payload Message content.
 * @param qos Quality of Service (0, 1, or 2).
 * @return true Message published successfully.
 * @return false Timeout, modem returned ERROR, or publish failed.
 */
bool bc660k::mqtt_publish(const char* topic, const char* payload, int qos)
{
    char cmd[256];

    if (qos == 0)
        snprintf(cmd, sizeof(cmd), "AT+QMTPUB=0,0,0,0,\"%s\"\r\n", topic);
    else
        snprintf(cmd, sizeof(cmd), "AT+QMTPUB=0,%d,%d,0,\"%s\"\r\n",
                 mqtt_msg_id++, qos, topic);

    at_send(&at, cmd);

    char line[256];
    if (!at_expect_prefix(&at, ">", line, sizeof(line), 5000))
        return false;

    at_send(&at, payload);
    uart_layer_write(&uart, "\x1A", 1);

    if (!at_expect_prefix(&at, "+QMTPUB:", line, sizeof(line), 10000))
        return false;

    return true;
}

/**
 * @brief Subscribe to MQTT topic.
 * @param topic Topic to subscribe.
 * @param qos Quality of Service (0, 1, or 2).
 * @return true Subscription accepted.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_subscribe(const char* topic, int qos)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QMTSUB=0,%d,\"%s\",%d\r\n",
             mqtt_msg_id++, topic, qos);

    at_send(&at, cmd);

    char line[256];
    return at_expect_prefix(&at, "+QMTSUB:", line, sizeof(line), 5000);
}

/**
 * @brief Unsubscribe from MQTT topic.
 * @param topic Topic to unsubscribe.
 * @return true Unsubscription accepted.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_unsubscribe(const char* topic)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QMTUNS=0,%d,\"%s\"\r\n",
             mqtt_msg_id++, topic);

    at_send(&at, cmd);

    char line[256];
    return at_expect_prefix(&at, "+QMTUNS:", line, sizeof(line), 5000);
}

/**
 * @brief Disconnect from MQTT broker.
 * @return true Disconnected successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_disconnect()
{
    at_send(&at, "AT+QMTDISC=0\r\n");

    char line[256];
    if (!at_expect_prefix(&at, "+QMTDISC: 0,0", line, sizeof(line), 5000))
        return false;

    mqtt_connected = false;
    return true;
}

/**
 * @brief Close MQTT TCP connection.
 * @return true Connection closed successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_close()
{
    at_send(&at, "AT+QMTCLOSE=0\r\n");

    char line[256];
    if (!at_expect_prefix(&at, "+QMTCLOSE: 0,0", line, sizeof(line), 5000))
        return false;

    mqtt_socket_open = false;
    return true;
}

/**
 * @brief Open a TCP or UDP socket connection.
 * @param socket_id Socket identifier (0–11).
 * @param protocol "TCP" or "UDP".
 * @param host Remote host (IP or domain).
 * @param port Remote port number.
 * @return true Socket opened successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::socket_open(int socket_id,
                         const char* protocol,
                         const char* host,
                         int port)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+QIOPEN=1,%d,\"%s\",\"%s\",%d,0,0\r\n",
             socket_id, protocol, host, port);

    at_send(&at, cmd);

    char line[256];
    char expected[32];
    snprintf(expected, sizeof(expected), "+QIOPEN: %d,0", socket_id);

    if (!at_expect_prefix(&at, expected, line, sizeof(line), 15000))
        return false;

    return true;
}

/**
 * @brief Close an open socket.
 * @param socket_id Socket identifier (0–11).
 * @return true Socket closed successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::socket_close(int socket_id)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QICLOSE=%d\r\n", socket_id);

    at_send(&at, cmd);
    return at_expect_ok(&at, 5000);
}

/**
 * @brief Send data through an open socket.
 * @param socket_id Socket identifier (0–11).
 * @param data Pointer to data buffer.
 * @param data_len Number of bytes to send.
 * @return true Data sent successfully.
 * @return false Timeout, modem returned ERROR, or send failed.
 */
bool bc660k::socket_send(int socket_id,
                         const char* data,
                         int data_len)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QISEND=%d,%d\r\n", socket_id, data_len);

    at_send(&at, cmd);

    char line[256];
    if (!at_expect_prefix(&at, ">", line, sizeof(line), 5000))
        return false;

    uart_layer_write(&uart, data, data_len);
    uart_layer_write(&uart, "\x1A", 1);

    if (!at_expect_prefix(&at, "+QISEND:", line, sizeof(line), 10000))
        return false;

    return true;
}

/**
 * @brief Receive data from an open socket.
 * @param socket_id Socket identifier (0–11).
 * @param out Output buffer for received data.
 * @param max_len Maximum number of bytes to read.
 * @param timeout_ms Timeout in milliseconds.
 * @return true Data received successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::socket_receive(int socket_id,
                            char* out,
                            int max_len,
                            int timeout_ms)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QIRD=%d,%d\r\n", socket_id, max_len);

    at_send(&at, cmd);

    char line[256];
    if (!at_expect_prefix(&at, "+QIRD:", line, sizeof(line), timeout_ms))
        return false;

    int len = 0;
    sscanf(line, "+QIRD: %d", &len);

    if (len <= 0)
        return false;

    int r = uart_layer_read(&uart, out, len, timeout_ms);
    if (r <= 0)
        return false;

    out[r] = '\0';
    return true;
}