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
 * @param tx TX GPIO pin.
 * @param rx RX GPIO pin.
 * @param port UART port number.
 * @param baud_rate UART baud rate.
 * @return void
 */
void bc660k::uart_layer_init(gpio_num_t tx,
                             gpio_num_t rx,
                             uart_port_t port,
                             int baud_rate)
{
    uart.port = port;
    uart.default_timeout_ms = 100;

    gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    gpio_set_direction(rx, GPIO_MODE_INPUT);
    gpio_set_pull_mode(rx, GPIO_PULLUP_ONLY);

    uart_config_t cfg = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT};

    uart_param_config(port, &cfg);
    uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(port, 2048, 1024, 0, NULL, 0);
}

/**
 * @brief Write raw bytes to UART.
 * @param data Pointer to data buffer.
 * @param len Number of bytes to send.
 * @return Number of bytes written.
 * @return 0 If data is null or len <= 0.
 */
int bc660k::uart_layer_write(const char *data, int len)
{
    if (!data || len <= 0)
        return 0;
    return uart_write_bytes(uart.port, data, len);
}

/**
 * @brief Read raw bytes from UART.
 * @param buf Output buffer.
 * @param max_len Maximum bytes to read.
 * @param timeout_ms Timeout in milliseconds.
 * @return Number of bytes read.
 * @return 0 If buffer is null or max_len <= 0.
 */
int bc660k::uart_layer_read(char *buf, int max_len, int timeout_ms)
{
    if (!buf || max_len <= 0)
        return 0;
    return uart_read_bytes(uart.port,
                           reinterpret_cast<uint8_t *>(buf),
                           max_len,
                           timeout_ms / portTICK_PERIOD_MS);
}

/**
 * @brief Flush UART RX buffer.
 * @return void
 */
void bc660k::uart_layer_flush()
{
    uart_flush(uart.port);
}

/**
 * @brief Initialize AT command layer.
 * @return void
 */
void bc660k::at_layer_init()
{
    at.uart = &uart;
    memset(at.line_buf, 0, sizeof(at.line_buf));
}

/**
 * @brief Send AT command string.
 * @param cmd Null-terminated AT command string.
 * @return true Command sent successfully.
 * @return false Invalid command or zero length.
 */
bool bc660k::at_send(const char *cmd)
{
    if (!cmd)
        return false;
    int len = strlen(cmd);
    if (len <= 0)
        return false;

    uart_layer_flush();
    uart_layer_write(cmd, len);
    return true;
}

/**
 * @brief Read a full line from UART until '\n'.
 * @param out Output buffer for line.
 * @param max_len Maximum buffer size.
 * @param timeout_ms Timeout in milliseconds.
 * @return true Line read successfully.
 * @return false Timeout or no complete line.
 */
bool bc660k::at_read_line(char *out, int max_len, int timeout_ms)
{
    int idx = 0;
    int elapsed = 0;

    while (elapsed < timeout_ms)
    {
        char c;
        int r = uart_layer_read(&c, 1, 10);

        if (r == 1)
        {
            if (c == '\n')
            {
                if (idx == 0)
                    continue; 
                out[idx] = '\0';
                return true;
            }

            if (c != '\r' && idx < max_len - 1)
            {
                out[idx++] = c;
            }
        }

        elapsed += 10;
    }

    return false;
}

/**
 * @brief Wait for "OK" or "ERROR" response.
 * @param timeout_ms Timeout in milliseconds.
 * @return true If "OK" received.
 * @return false If "ERROR" or timeout.
 */
bool bc660k::at_expect_ok(int timeout_ms)
{
    char line[256];

    while (at_read_line(line, sizeof(line), timeout_ms))
    {
        if (strcmp(line, "OK") == 0)
            return true;

        if (strcmp(line, "ERROR") == 0)
            return false;
    }

    return false;
}

/**
 * @brief Wait for a response starting with a specific prefix.
 * @param prefix Expected prefix string.
 * @param out Output buffer for matched line.
 * @param max_len Maximum buffer size.
 * @param timeout_ms Timeout in milliseconds.
 * @return true Prefix matched successfully.
 * @return false Timeout or "ERROR" received.
 */
bool bc660k::at_expect_prefix(const char *prefix,
                              char *out,
                              int max_len,
                              int timeout_ms)
{
    char line[256];

    while (at_read_line(line, sizeof(line), timeout_ms))
    {
        /* Sidetrack inbound MQTT messages so they are never lost during
         * an unrelated AT command exchange. */
        if (strncmp(line, "+QMTRECV:", 9) == 0)
        {
            urc_recv_push(line);
            continue;
        }

        if (strncmp(line, prefix, strlen(prefix)) == 0)
        {
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
 */
bc660k::bc660k()
{
    mqtt_connected = false;
    mqtt_socket_open = false;
    mqtt_msg_id = 1;

    memset(&uart, 0, sizeof(uart));
    memset(&at, 0, sizeof(at));
    memset(_urc_recv_buf, 0, sizeof(_urc_recv_buf));
    _urc_recv_head = 0;
    _urc_recv_tail = 0;
}

/**
 * @brief Initialize BC660K modem driver and UART interface.
 */
bool bc660k::init(gpio_num_t tx,
                  gpio_num_t rx,
                  gpio_num_t rst,
                  uart_port_t port,
                  int baud_rate)
{
    reset_pin = rst;

    gpio_set_direction(reset_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(reset_pin, 1);

    uart_layer_init(tx, rx, port, baud_rate);
    at_layer_init();
    uart_layer_flush();

    if (!try_at(3, 1000))
        reset_modem();

    ESP_LOGI(__func__, "Disabling deep sleep...");
    disable_deepsleep();

    ESP_LOGI(__func__, "Checking SIM status...");
    if (!wait_sim_ready(3))
        return false;

    ESP_LOGI(__func__, "Disabling echo...");
    disable_echo();

    return true;
}

/**
 * @brief Performs a hardware reset on the BC660K modem.
 * @return void
 */
void bc660k::reset_modem()
{
    gpio_set_level(reset_pin, 0);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    gpio_set_level(reset_pin, 1);
    vTaskDelay(1500 / portTICK_PERIOD_MS); 
}

/**
 * @brief Attempts to communicate with the modem by sending "AT" and waiting for "OK".
 *
 * @param attempts Number of attempts before giving up.
 * @param timeout_ms Timeout in milliseconds for each AT response.
 *
 * @return true  If the modem responded with "OK" within the allowed attempts.
 * @return false If all attempts failed, even after resetting the modem.
 */
bool bc660k::try_at(int attempts, int timeout_ms)
{
    char line[64];

    for (int i = 0; i < attempts; i++)
    {
        at_send("AT\r\n");

        if (at_read_line(line, sizeof(line), timeout_ms))
        {
            if (strcmp(line, "OK") == 0)
                return true;
        }
        else
        {
            ESP_LOGE(__func__, "No response received (attempt %d)", i + 1);
        }

        reset_modem();
    }

    return false;
}

/**
 * @brief Disables deep sleep mode using AT+QSCLK=0.
 * @return true If command succeeded.
 * @return false Otherwise.
 */

bool bc660k::disable_deepsleep()
{
    at_send("AT+QSCLK=0\r\n");
    return at_expect_ok(3000);
}

/**
 * @brief Checks SIM card status using AT+CPIN?.
 * @return true If SIM is ready.
 * @return false Otherwise.
 */
bool bc660k::get_cpin_status()
{
    char line[64];

    at_send("AT+CPIN?\r\n");

    if (at_expect_prefix("+CPIN: READY", line, sizeof(line), 3000))
        return true;

    return false;
}
/**
 * @brief Waits for SIM card to become ready.
 * @param attempts Number of retries.
 * @return true If SIM becomes ready.
 * @return false If SIM never becomes ready.
 */

bool bc660k::wait_sim_ready(int attempts)
{
    for (int i = 0; i < attempts; i++)
    {
        if (get_cpin_status())
            return true;

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    return false;
}

/**
 * @brief Disables modem echo using ATE0.
 * @return true If echo was disabled successfully.
 * @return false Otherwise.
 */

bool bc660k::disable_echo()
{
    at_send("ATE0\r\n");
    return at_expect_ok(3000);
}
/**
 * @brief Reads SIM ICCID using AT+QCCID.
 * @param out Output buffer for ICCID string.
 * @return true If ICCID was read successfully.
 * @return false Otherwise.
 */
bool bc660k::get_iccid(char *out)
{
    at_send("AT+QCCID\r\n");
    return at_expect_prefix("+QCCID:", out, 64, 3000);
}

/**
 * @brief Enables connection mode using AT+CSCON=1.
 * @return true If command succeeded.
 * @return false Otherwise.
 */

bool bc660k::enable_connection()
{
    at_send("AT+CSCON=1\r\n");
    return at_expect_ok(3000);
}

/**
 * @brief Waits for +IP: response indicating PDP activation.
 * @param timeout_ms Timeout in milliseconds.
 * @return true If IP was obtained.
 * @return false Otherwise.
 */
bool bc660k::wait_for_ip(int timeout_ms, char *out_ip)
{
    char line[128];
    int elapsed = 0;

    while (elapsed < timeout_ms)
    {
        at_send("AT+CGPADDR\r\n");

        if (at_expect_prefix("+CGPADDR:", line, sizeof(line), 2000))
        {
            char *comma = strchr(line, ',');
            if (comma)
            {
                char *ip = comma + 1;

                if (*ip == '"')
                    ip++;
                char *end = strchr(ip, '"');
                if (end)
                    *end = '\0';

                if (strlen(ip) > 0 && strcmp(ip, "0.0.0.0") != 0)
                {
                    strcpy(out_ip, ip);
                    return true;
                }
            }
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        elapsed += 1000;
    }

    return false;
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
bool bc660k::set_apn(const char *pdp,
                     const char *apn,
                     const char *user,
                     const char *pass)
{
    char cmd[128];

    if (strlen(user) > 0 && strlen(pass) > 0)
        snprintf(cmd, sizeof(cmd), "AT+QCGDEFCONT=\"%s\",\"%s\",\"%s\",\"%s\"\r\n",
                 pdp, apn, user, pass);
    else
        snprintf(cmd, sizeof(cmd), "AT+QCGDEFCONT=\"%s\",\"%s\"\r\n",
                 pdp, apn);

    at_send(cmd);
    return at_expect_ok(3000);
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

    at_send(cmd);
    return at_expect_ok(3000);
}

/**
 * @brief Configure operator selection.
 * @param mode 0 = automatic, 1 = manual.
 * @param format 0 long alphanumeric, 1 short, 2 numeric.
 * @param oper Operator MCC/MNC when mode != 0.
 * @return true Operator configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_operator(int mode, int format, const char *oper)
{
    char cmd[64];

    if (mode == 0)
        snprintf(cmd, sizeof(cmd), "AT+COPS=0\r\n");
    else
        snprintf(cmd, sizeof(cmd), "AT+COPS=%d,%d,\"%s\"\r\n", mode, format, oper);

    at_send(cmd);
    return at_expect_ok(5000);
}

/**
 * @brief Enable network registration URC notifications.
 * @return true URC enabled successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::enable_network_registration()
{
    at_send("AT+CEREG=1\r\n");
    return at_expect_ok(2000);
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

    while (elapsed < timeout_ms)
    {
        at_send("AT+CEREG?\r\n");

        if (at_expect_prefix("+CEREG:", line, sizeof(line), 2000))
        {
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
bool bc660k::get_operator(char *out_mccmnc)
{
    char line[256];

    at_send("AT+COPS?\r\n");

    if (!at_expect_prefix("+COPS:", line, sizeof(line), 2000))
        return false;

    char *p = strchr(line, '"');
    if (!p)
        return false;

    p++;
    char *end = strchr(p, '"');
    if (!end)
        return false;

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
bool bc660k::get_rssi(int *rssi_dbm)
{
    char line[256];

    at_send("AT+CSQ\r\n");

    if (!at_expect_prefix("+CSQ:", line, sizeof(line), 2000))
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
bool bc660k::get_time(char *out)
{
    char line[128];

    at_send("AT+CCLK?\r\n");

    if (!at_expect_prefix("+CCLK:", line, sizeof(line), 2000))
        return false;


    char *p = strchr(line, ':');
    if (!p)
        return false;
    p++;

    while (*p == ' ' || *p == '"')
        p++;

    unsigned long year, month, day, hour, minute, second;
    char tz[8] = {0};

    int n = sscanf(p, "%lu/%lu/%lu,%lu:%lu:%lu%7s",
                   &year, &month, &day,
                   &hour, &minute, &second,
                   tz);

    if (n < 6)
        return false;

    snprintf(out, 64, "%02lu/%02lu/%02lu,%02lu:%02lu:%02lu%s",
             year, month, day,
             hour, minute, second,
             tz);

    return true;
}

/**
 * @brief Get network date/time as a UTC epoch (seconds since 1970-01-01).
 *
 * Parses +CCLK, which reports local-modem time with a timezone offset in
 * quarter-hours, and converts to UTC using a self-contained Gregorian
 * algorithm (no dependency on the process TZ env var).
 *
 * @param out_epoch Output UTC epoch.
 * @return true On success.
 * @return false Timeout or parsing error.
 */
bool bc660k::get_time_utc(time_t *out_epoch)
{
    if (!out_epoch)
        return false;

    char line[128];

    at_send("AT+CCLK?\r\n");

    if (!at_expect_prefix("+CCLK:", line, sizeof(line), 2000))
        return false;

    char *p = strchr(line, ':');
    if (!p)
        return false;
    p++;
    while (*p == ' ' || *p == '"')
        p++;

    unsigned int year, month, day, hour, minute, second;
    char sign = '+';
    int tz_quarters = 0;
    int n = sscanf(p, "%u/%u/%u,%u:%u:%u%c%d",
                   &year, &month, &day,
                   &hour, &minute, &second,
                   &sign, &tz_quarters);

    if (n < 6)
        return false;

    // CCLK 2-digit year: 00..99 maps to 2000..2099.
    int y = 2000 + (int)year;
    int m = (int)month;
    int d = (int)day;

    // Days from civil (y, m, d) to 1970-01-01, Gregorian.
    // Ref: http://howardhinnant.github.io/date_algorithms.html (days_from_civil)
    int yy = y - (m <= 2 ? 1 : 0);
    int era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (153U * (unsigned)(m > 2 ? m - 3 : m + 9) + 2U) / 5U + (unsigned)d - 1U;
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;

    int64_t epoch = days * 86400
                  + (int64_t)hour * 3600
                  + (int64_t)minute * 60
                  + (int64_t)second;

    if (n >= 8) {
        int64_t offset_sec = (int64_t)tz_quarters * 15 * 60;
        if (sign == '-')
            offset_sec = -offset_sec;
        epoch -= offset_sec;
    }

    *out_epoch = (time_t)epoch;
    return true;
}

/**
 * @brief Wait until the modem enters RRC Connected state.
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return true Modem entered RRC Connected state.
 * @return false Timeout or error.
 */
bool bc660k::wait_rrc_connected(int timeout_ms)
{
    char line[64];
    int elapsed = 0;

    while (elapsed < timeout_ms)
    {
        at_send("AT+CSCON?\r\n");

        if (at_expect_prefix("+CSCON:", line, sizeof(line), 2000))
        {
            int mode = -1;
            if (sscanf(line, "+CSCON: %d", &mode) == 1)
            {
                if (mode == 1)
                    return true;
            }
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
        elapsed += 500;
    }

    return false;
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

    at_send(cmd);
    return at_expect_ok(3000);
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

    at_send(cmd);
    return at_expect_ok(3000);
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

    at_send(cmd);
    return at_expect_ok(3000);
}

/**
 * @brief Set network scan sequence.
 * @param seq Sequence string (e.g., "00" or "030201").
 * @return true Sequence configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_scan_sequence(const char *seq)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"nwscanseq\",%s\r\n", seq);

    at_send(cmd);
    return at_expect_ok(3000);
}

/**
 * @brief Configure extended band mask.
 * @param mask Band mask string (e.g., "0,80000,80000").
 * @return true Band mask configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_band_extended(const char *mask)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QCFG=\"band\",%s\r\n", mask);

    at_send(cmd);
    return at_expect_ok(3000);
}

/**
 * @brief Get network information (technology, band, operator).
 * @param out Output buffer for QNWINFO response.
 * @return true Information retrieved successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::get_nwinfo(char *out)
{
    char line[256];

    at_send("AT+QNWINFO\r\n");

    if (!at_expect_prefix("+QNWINFO:", line, sizeof(line), 2000))
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
bool bc660k::get_serving_cell(char *out)
{
    char line[256];

    at_send("AT+QENG=\"servingcell\"\r\n");

    if (!at_expect_prefix("+QENG:", line, sizeof(line), 3000))
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
bool bc660k::get_qcsq(int *rsrp, int *rsrq, int *snr)
{
    char line[256];

    at_send("AT+QCSQ\r\n");

    if (!at_expect_prefix("+QCSQ:", line, sizeof(line), 2000))
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
    at_send("AT+CGATT=1\r\n");
    return at_expect_ok(5000);
}

/**
 * @brief Check if the modem is attached to the network.
 * @param attached Output pointer: true if attached, false otherwise.
 * @return true Status retrieved successfully.
 * @return false Timeout or parsing error.
 */
bool bc660k::is_attached(bool *attached)
{
    char line[256];

    at_send("AT+CGATT?\r\n");

    if (!at_expect_prefix("+CGATT:", line, sizeof(line), 2000))
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

    at_send(cmd);
    return at_expect_ok(15000);
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

    at_send(cmd);
    return at_expect_ok(10000);
}

/**
 * @brief Configure Power Saving Mode (PSM) parameters.
 * @param tau Requested periodic TAU value (e.g., "00100001").
 * @param active_time Requested Active Time value (e.g., "00000011").
 * @return true PSM configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_psm(const char *tau, const char *active_time)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QPSMS=1,,,\"%s\",\"%s\"\r\n", tau, active_time);

    at_send(cmd);
    return at_expect_ok(3000);
}

/**
 * @brief Configure eDRX parameters.
 * @param mode eDRX mode (e.g., "2" for NB-IoT).
 * @param edrx_value eDRX cycle value (e.g., "0101").
 * @return true eDRX configured successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::set_edrx(const char *mode, const char *edrx_value)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CEDRXS=%s,\"%s\"\r\n", mode, edrx_value);

    at_send(cmd);
    return at_expect_ok(3000);
}
/**
 * @brief Configure MQTT settings.
 *
 * @return true Configuration successful.
 * @return false Configuration failed.
 */
bool bc660k::mqtt_configure()
{

    at_send("AT+QMTCFG=\"will\",0\r\n");
    if (!at_expect_ok(3000))
    {
        ESP_LOGE(__func__, "Failed to set MQTT will configuration");
        return false;
    }

    at_send("AT+QMTCFG=\"keepalive\",0,60\r\n");
    if (!at_expect_ok(3000))
    {
        ESP_LOGE(__func__, "Failed to set MQTT keepalive configuration");
        return false;
    }

    at_send("AT+QMTCFG=\"timeout\",0,30\r\n");
    if (!at_expect_ok(3000))
    {
        ESP_LOGE(__func__, "Failed to set MQTT timeout configuration");
        return false;
    }

    return true;
}
/**
 * @brief Check if MQTT is already open for a given host and port.
 *
 * @param host The MQTT broker hostname or IP address.
 * @param port The MQTT broker port number.
 * @return true If the MQTT connection is already open for the given host and port.
 * @return false If the MQTT connection is not open or an error occurred.
 */

bool bc660k::mqtt_is_open(const char *host, int port)
{
    char line[256];

    at_send("AT+QMTOPEN?\r\n");

    if (!at_expect_prefix("+QMTOPEN:", line, sizeof(line), 2000))
        return false;

    if (strstr(line, "+QMTOPEN: 0,0"))
        return true;

    char expected[128];
    snprintf(expected, sizeof(expected), "+QMTOPEN: 0,\"%s\",%d", host, port);

    if (strstr(line, expected))
        return true;

    return false;
}

/**
 * @brief Open MQTT TCP connection to broker.
 * @param host Broker hostname or IP address.
 * @param port Broker port number (usually 1883).
 * @return true Connection opened successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_open(const char *host, int port)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"%s\",%d\r\n", host, port);

    at_send(cmd);

    if (!at_expect_ok(10000))
    {
        ESP_LOGE("mqtt_open", "Did not receive OK after QMTOPEN");
        return false;
    }

    char line[256];
    if (!at_expect_prefix("+QMTOPEN:", line, sizeof(line), 15000))
    {
        ESP_LOGE("mqtt_open", "No +QMTOPEN URC received");
        return false;
    }

    int client = -1;
    int result = -1;
    sscanf(line, "+QMTOPEN: %d,%d", &client, &result);

    if (result != 0)
    {
        ESP_LOGE("mqtt_open", "MQTT open failed with code %d", result);
        return false;
    }

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
bool bc660k::mqtt_connect(const char *client_id, const char *user, const char *pass)
{
    char cmd[256];

    if (user == nullptr || strlen(user) == 0)
    {
        snprintf(cmd, sizeof(cmd),
                 "AT+QMTCONN=0,\"%s\"\r\n",
                 client_id);
    }
    else
    {
        snprintf(cmd, sizeof(cmd),
                 "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"\r\n",
                 client_id, user, pass);
    }

    at_send(cmd);

    if (!at_expect_ok(10000))
    {
        ESP_LOGE("mqtt_connect", "Did not receive OK after QMTCONN");
        return false;
    }

    char line[256];
    if (!at_expect_prefix("+QMTCONN:", line, sizeof(line), 15000))
    {
        ESP_LOGE("mqtt_connect", "MQTT connect failed or timed out - response: %s", line);
        return false;
    }

    int client = -1;
    int result = -1;
    int code = -1;

    if (sscanf(line, "+QMTCONN: %d,%d,%d", &client, &result, &code) != 3)
    {
        ESP_LOGE("mqtt_connect", "Failed to parse QMTCONN response");
        return false;
    }

    if (result != 0)
    {
        ESP_LOGE("mqtt_connect", "MQTT connect failed: result=%d code=%d", result, code);
        return false;
    }

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
bool bc660k::mqtt_publish(const char *topic, const char *payload, int qos)
{
    char cmd[256];

    if (qos == 0)
        snprintf(cmd, sizeof(cmd), "AT+QMTPUB=0,0,0,0,\"%s\"\r\n", topic);
    else
        snprintf(cmd, sizeof(cmd), "AT+QMTPUB=0,%d,%d,0,\"%s\"\r\n",
                 mqtt_msg_id++, qos, topic);

    at_send(cmd);

    char line[256];
    if (!at_expect_prefix(">", line, sizeof(line), 5000))
        return false;

    at_send(payload);
    uart_layer_write("\x1A", 1);

    if (!at_expect_prefix("+QMTPUB:", line, sizeof(line), 10000))
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
bool bc660k::mqtt_subscribe(const char *topic, int qos)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QMTSUB=0,%d,\"%s\",%d\r\n",
             mqtt_msg_id++, topic, qos);

    at_send(cmd);

    char line[256];
    return at_expect_prefix("+QMTSUB:", line, sizeof(line), 5000);
}

/**
 * @brief Unsubscribe from MQTT topic.
 * @param topic Topic to unsubscribe.
 * @return true Unsubscription accepted.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_unsubscribe(const char *topic)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QMTUNS=0,%d,\"%s\"\r\n",
             mqtt_msg_id++, topic);

    at_send(cmd);

    char line[256];
    return at_expect_prefix("+QMTUNS:", line, sizeof(line), 5000);
}

/**
 * @brief Disconnect from MQTT broker.
 * @return true Disconnected successfully.
 * @return false Timeout or modem returned ERROR.
 */
bool bc660k::mqtt_disconnect()
{
    at_send("AT+QMTDISC=0\r\n");

    char line[256];
    if (!at_expect_prefix("+QMTDISC: 0,0", line, sizeof(line), 5000))
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
    at_send("AT+QMTCLOSE=0\r\n");

    char line[256];
    if (!at_expect_prefix("+QMTCLOSE: 0,0", line, sizeof(line), 5000))
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
                         const char *protocol,
                         const char *host,
                         int port)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+QIOPEN=1,%d,\"%s\",\"%s\",%d,0,0\r\n",
             socket_id, protocol, host, port);

    at_send(cmd);

    char line[256];
    char expected[32];
    snprintf(expected, sizeof(expected), "+QIOPEN: %d,0", socket_id);

    if (!at_expect_prefix(expected, line, sizeof(line), 15000))
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

    at_send(cmd);
    return at_expect_ok(5000);
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
                         const char *data,
                         int data_len)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QISEND=%d,%d\r\n", socket_id, data_len);

    at_send(cmd);

    char line[256];
    if (!at_expect_prefix(">", line, sizeof(line), 5000))
        return false;

    uart_layer_write(data, data_len);
    uart_layer_write("\x1A", 1);

    if (!at_expect_prefix("+QISEND:", line, sizeof(line), 10000))
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
                            char *out,
                            int max_len,
                            int timeout_ms)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QIRD=%d,%d\r\n", socket_id, max_len);

    at_send(cmd);

    char line[256];
    if (!at_expect_prefix("+QIRD:", line, sizeof(line), timeout_ms))
        return false;

    int len = 0;
    sscanf(line, "+QIRD: %d", &len);

    if (len <= 0)
        return false;

    int r = uart_layer_read(out, len, timeout_ms);
    if (r <= 0)
        return false;

    out[r] = '\0';
    return true;
}

/**
 * @brief Push a raw `+QMTRECV:` URC line into the internal ring buffer.
 *
 * @details Called exclusively from @ref at_expect_prefix whenever it intercepts
 * a `+QMTRECV:` line while waiting for another command response. If the ring
 * buffer is full the oldest entry is silently overwritten (head advances).
 *
 * @param line Null-terminated raw URC line starting with `+QMTRECV:`.
 */
void bc660k::urc_recv_push(const char *line)
{
    int next_head = (_urc_recv_head + 1) % URC_RECV_SLOTS;

    if (next_head == _urc_recv_tail)
    {
        /* Buffer full — drop oldest entry by advancing tail. */
        ESP_LOGW("bc660k", "URC recv buffer full, dropping oldest message");
        _urc_recv_tail = (_urc_recv_tail + 1) % URC_RECV_SLOTS;
    }

    strncpy(_urc_recv_buf[_urc_recv_head], line, URC_RECV_LINE_MAX - 1);
    _urc_recv_buf[_urc_recv_head][URC_RECV_LINE_MAX - 1] = '\0';
    _urc_recv_head = next_head;
}

/**
 * @brief Retrieve one inbound MQTT message from the URC ring buffer.
 *
 * @details The BC660K pushes received MQTT messages as an unsolicited result
 * code in the following format (BC660K-GL MQTT Application Note §3.3):
 * @code
 * +QMTRECV: <TCP_connectID>,<msgID>,<topic>[,<payload_len>],<payload>
 * @endcode
 *
 * This function first checks the internal URC ring buffer populated by
 * @ref urc_recv_push. If the buffer is empty and @p timeout_ms > 0 it reads
 * raw lines from the UART until a `+QMTRECV:` line arrives or the timeout
 * expires. The parsed topic and payload are written to the caller's buffers.
 *
 * @param topic       Output buffer for the message topic string.
 * @param topic_len   Size of @p topic buffer in bytes.
 * @param payload     Output buffer for the message payload string.
 * @param payload_len Size of @p payload buffer in bytes.
 * @param timeout_ms  Maximum time to wait for a new message (0 = non-blocking).
 * @return true  A message was successfully parsed.
 * @return false No message available within @p timeout_ms.
 */
bool bc660k::mqtt_receive(char *topic,   size_t topic_len,
                          char *payload, size_t payload_len,
                          int timeout_ms)
{
    char raw[URC_RECV_LINE_MAX];

    /* ── 1. Check ring buffer first (messages captured during AT exchanges) ── */
    if (_urc_recv_tail != _urc_recv_head)
    {
        strncpy(raw, _urc_recv_buf[_urc_recv_tail], sizeof(raw) - 1);
        raw[sizeof(raw) - 1] = '\0';
        _urc_recv_tail = (_urc_recv_tail + 1) % URC_RECV_SLOTS;
        goto parse;
    }

    /* ── 2. Optionally wait for a new URC on the UART ─────────────────────── */
    if (timeout_ms > 0)
    {
        if (!at_expect_prefix("+QMTRECV:", raw, sizeof(raw), timeout_ms))
            return false;
        goto parse;
    }

    return false;

parse:
    /*
     * Raw line format (fields separated by commas, topic and payload unquoted
     * per the BC660K-GL AT manual when QMTCFG "recv/mode" is 0):
     *
     *   +QMTRECV: <conn_id>,<msg_id>,"<topic>",<payload_len>,"<payload>"
     *
     * We locate the topic by finding the first '"', and the payload by
     * finding the third '"' (after conn_id and msg_id fields).
     */
    {
        /* Skip prefix "+QMTRECV: " */
        const char *p = strchr(raw, ':');
        if (!p) return false;
        p++; /* skip ':' */
        while (*p == ' ') p++;

        /* Skip conn_id and msg_id (two comma-separated integers) */
        for (int commas = 0; commas < 2; commas++)
        {
            p = strchr(p, ',');
            if (!p) return false;
            p++; /* skip ',' */
        }

        /* ── Parse topic ── */
        if (*p == '"') p++; /* skip opening quote if present */
        const char *topic_start = p;
        const char *topic_end   = strchr(p, '"');
        if (!topic_end)
        {
            /* Topic without quotes — delimited by the next comma. */
            topic_end = strchr(p, ',');
            if (!topic_end) return false;
        }

        size_t tlen = (size_t)(topic_end - topic_start);
        if (tlen >= topic_len) tlen = topic_len - 1;
        strncpy(topic, topic_start, tlen);
        topic[tlen] = '\0';

        /* ── Skip to payload (past optional payload_len field) ── */
        p = topic_end;
        if (*p == '"') p++; /* skip closing topic quote */
        p = strchr(p, ',');
        if (!p) return false;
        p++; /* skip ',' */

        /* If the next token looks like a number it is payload_len — skip it. */
        if (*p >= '0' && *p <= '9')
        {
            p = strchr(p, ',');
            if (!p) return false;
            p++; /* skip ',' */
        }

        /* ── Parse payload ── */
        if (*p == '"') p++; /* skip opening quote if present */
        const char *payload_start = p;

        /* Strip trailing quote and/or CR/LF */
        size_t plen = strlen(payload_start);
        while (plen > 0 && (payload_start[plen - 1] == '"'  ||
                             payload_start[plen - 1] == '\r' ||
                             payload_start[plen - 1] == '\n'))
        {
            plen--;
        }

        if (plen >= payload_len) plen = payload_len - 1;
        strncpy(payload, payload_start, plen);
        payload[plen] = '\0';
    }

    return true;
}