#ifndef BC660K_HPP
#define BC660K_HPP

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

typedef struct
{
    uart_port_t port;
    int default_timeout_ms;
} uart_layer_t;

static void uart_layer_init(uart_layer_t *u,
                            gpio_num_t tx,
                            gpio_num_t rx,
                            uart_port_t port,
                            int baud_rate);

static int uart_layer_write(uart_layer_t *u, const char *data, int len);
static int uart_layer_read(uart_layer_t *u, char *buf, int max_len, int timeout_ms);
static void uart_layer_flush(uart_layer_t *u);

typedef struct
{
    uart_layer_t *uart;
    char line_buf[256];
} at_layer_t;

static void at_layer_init(at_layer_t *at, uart_layer_t *uart);
static bool at_send(at_layer_t *at, const char *cmd);
static bool at_read_line(at_layer_t *at, char *out, int max_len, int timeout_ms);
static bool at_expect_ok(at_layer_t *at, int timeout_ms);
static bool at_expect_prefix(at_layer_t *at,
                             const char *prefix,
                             char *out,
                             int max_len,
                             int timeout_ms);

class bc660k
{
private:
    uart_layer_t uart;
    at_layer_t at;

    int mqtt_msg_id;
    bool mqtt_connected;
    bool mqtt_socket_open;

public:
    bc660k();

    bool init(gpio_num_t tx,
              gpio_num_t rx,
              uart_port_t port,
              int baud_rate);

    bool set_apn(const char *pdp,
                 const char *apn,
                 const char *user,
                 const char *pass);

    bool set_band(int band);
    bool set_operator(int mode, int format, const char *oper);
    bool enable_network_registration();
    bool wait_network_registered(int timeout_ms);
    bool get_operator(char *out_mccmnc);
    bool get_rssi(int *rssi_dbm);
    bool get_time(char *out_datetime);

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

    bool mqtt_config_will(int enable);
    bool mqtt_open(const char *host, int port);
    bool mqtt_connect(const char *client_id, const char *user, const char *pass);
    bool mqtt_publish(const char *topic, const char *payload, int qos);
    bool mqtt_subscribe(const char *topic, int qos);
    bool mqtt_unsubscribe(const char *topic);
    bool mqtt_disconnect();
    bool mqtt_close();

    bool socket_open(int socket_id, const char *protocol, const char *host, int port);
    bool socket_close(int socket_id);
    bool socket_send(int socket_id, const char *data, int data_len);
    bool socket_receive(int socket_id, char *out, int max_len, int timeout_ms);
};

#endif