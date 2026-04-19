#ifndef BC660K_HPP
#define BC660K_HPP

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

typedef struct
{
    uart_port_t port;
    int default_timeout_ms;
} uart_layer_t;

typedef struct
{
    uart_layer_t *uart;
    char line_buf[256];
} at_layer_t;

class bc660k
{
private:
    uart_layer_t uart;
    at_layer_t at;

    int mqtt_msg_id;
    bool mqtt_connected;
    bool mqtt_socket_open;
    gpio_num_t reset_pin;

    void uart_layer_init(gpio_num_t tx, gpio_num_t rx, uart_port_t port, int baud_rate);
    int uart_layer_write(const char *data, int len);
    int uart_layer_read(char *buf, int max_len, int timeout_ms);
    void uart_layer_flush();

    void at_layer_init();
    bool at_send(const char *cmd);
    bool at_read_line(char *out, int max_len, int timeout_ms);
    bool at_expect_ok(int timeout_ms);
    bool at_expect_prefix(const char *prefix, char *out, int max_len, int timeout_ms);

    static constexpr int URC_RECV_SLOTS = 4;      /**< Maximum queued inbound messages. */
    static constexpr int URC_RECV_LINE_MAX = 384; /**< Maximum raw URC line length. */

    char _urc_recv_buf[URC_RECV_SLOTS][URC_RECV_LINE_MAX];
    int _urc_recv_head = 0; /**< Write index (producer). */
    int _urc_recv_tail = 0; /**< Read index (consumer). */

    void urc_recv_push(const char *line);

    void reset_modem();
    bool try_at(int attempts, int timeout_ms);
    bool get_cpin_status();
    bool wait_sim_ready(int attempts);
    bool disable_echo();
    bool disable_deepsleep();

public:
    bc660k();

    bool init(gpio_num_t tx,
              gpio_num_t rx,
              gpio_num_t rst,
              uart_port_t port,
              int baud_rate);

    bool set_apn(const char *pdp,
                 const char *apn,
                 const char *user,
                 const char *pass);

    bool get_iccid(char *out);
    bool set_band(int band);
    bool enable_connection();
    bool wait_for_ip(int timeout_ms, char *out_ip);
    bool set_operator(int mode, int format, const char *oper);
    bool enable_network_registration();
    bool wait_network_registered(int timeout_ms);
    bool get_operator(char *out_mccmnc);
    bool get_rssi(int *rssi_dbm);
    bool get_time(char *out_datetime);
    bool get_time_utc(time_t *out_epoch);
    bool wait_rrc_connected(int timeout_ms);

    bool set_nwscanmode(int mode);
    bool set_iotopmode(int mode);
    bool set_roaming(int enable);
    bool set_scan_sequence(const char *seq);
    bool set_band_extended(const char *mask);

    bool get_nwinfo(char *out);
    bool get_serving_cell(char *out);
    bool get_qcsq(int *rsrp, int *rsrq, int *snr);

    bool attach();
    bool is_attached(bool *attached);
    bool activate_pdp(int cid);
    bool deactivate_pdp(int cid);

    bool set_psm(const char *tau, const char *active_time);
    bool set_edrx(const char *mode, const char *edrx_value);

    bool mqtt_is_open(const char *host, int port);
    bool mqtt_configure();
    bool mqtt_open(const char *host, int port);
    bool mqtt_connect(const char *client_id, const char *user, const char *pass);
    bool mqtt_publish(const char *topic, const char *payload, int qos);
    bool mqtt_subscribe(const char *topic, int qos);
    bool mqtt_unsubscribe(const char *topic);
    bool mqtt_disconnect();
    bool mqtt_close();
    bool mqtt_receive(char *topic, size_t topic_len, char *payload, size_t payload_len, int timeout_ms = 0);

    bool socket_open(int socket_id, const char *protocol, const char *host, int port);
    bool socket_close(int socket_id);
    bool socket_send(int socket_id, const char *data, int data_len);
    bool socket_receive(int socket_id, char *out, int max_len, int timeout_ms);
};

#endif