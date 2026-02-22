# **BC660K Driver**

Este projeto implementa um driver completo para o modem **Quectel BC660K‑GL** em **C/C++**, otimizado para ESP32, com foco em:

- Arquitetura modular  
- Zero STL  
- Zero malloc  
- Buffers fixos  
- Parsing determinístico  
- Camadas independentes (UART → AT → BC660K)  
- Suporte completo a **MQTT**, **TCP/UDP**, **PSM**, **eDRX**, **PDP**, **rede NB‑IoT**  

---

## 📡 Arquitetura da Biblioteca

A biblioteca é dividida em três camadas:

### **1. UART Layer**
Abstração de baixo nível para comunicação serial:

- Inicialização do UART  
- Escrita de bytes  
- Leitura com timeout  
- Flush do buffer RX  

### **2. AT Layer**
Camada intermediária responsável por:

- Envio de comandos AT  
- Leitura linha a linha  
- Espera por respostas específicas  
- Parsing seguro sem alocação dinâmica  

### **3. BC660K Driver**
Camada de alto nível que implementa:

- Configuração de rede  
- Registro na operadora  
- PDP context  
- PSM / eDRX  
- MQTT nativo  
- Sockets TCP/UDP  

---

## 🌐 Funcionalidades de Rede

### **Configurações**
- `set_apn()`  
- `set_band()` e `set_band_extended()`  
- `set_operator()`  
- `set_nwscanmode()`  
- `set_iotopmode()`  
- `set_roaming()`  
- `set_scan_sequence()`  

### **Diagnóstico**
- `get_rssi()`  
- `get_qcsq()` (RSRP/RSRQ/SNR)  
- `get_nwinfo()`  
- `get_serving_cell()`  
- `get_time()`  

### **Registro na Rede**
- `enable_network_registration()`  
- `wait_network_registered()`  

---

## 🔌 PDP Context

- `attach()`  
- `is_attached()`  
- `activate_pdp()`  
- `deactivate_pdp()`  

---

## 🔋 Power Saving

- `set_psm()`  
- `set_edrx()`  

---

## 📦 MQTT Nativo (via AT+QMT*)

Totalmente implementado:

- `mqtt_config_will()`  
- `mqtt_open()`  
- `mqtt_connect()`  
- `mqtt_publish()`  
- `mqtt_subscribe()`  
- `mqtt_unsubscribe()`  
- `mqtt_disconnect()`  
- `mqtt_close()`  

Suporta:

- QoS 0/1/2  
- Mensagens longas  
- Controle interno de `msg_id`  
- Parsing seguro de URCs  

---

## 🌍 Sockets TCP/UDP

Baseado nos comandos AT:

- `socket_open()`  
- `socket_close()`  
- `socket_send()`  
- `socket_receive()`  

Permite:

- TCP client  
- UDP client  
- Comunicação binária ou texto  
- Leitura com timeout real  

---

## 🚀 Exemplo de Uso (Resumo)

```cpp
modem.init(GPIO_NUM_17, GPIO_NUM_16, UART_NUM_1, 9600);
modem.set_apn("IP", "your_apn", "", "");
modem.set_nwscanmode(3);
modem.wait_network_registered(60000);
modem.activate_pdp(1);

modem.mqtt_open("test.mosquitto.org", 1883);
modem.mqtt_connect("clientID", "", "");
modem.mqtt_publish("topic/test", "Hello!", 0);
```

---

## 📁 Estrutura do Projeto

```
/src
  bc660k.hpp
  bc660k.cpp
/examples
  mqtt_demo.cpp
  tcp_demo.cpp
  udp_demo.cpp
README.md
```

---

## 🧩 Requisitos

- ESP32  
- ESP-IDF  
- Modem Quectel BC660K‑GL  
- Fonte de alimentação estável (picos de até 500mA)  

---

## 📌 Observações Importantes

- O BC660K exige **PDP ativo** antes de MQTT ou sockets.  
- Alguns comandos podem variar conforme firmware da operadora.  
- Recomenda-se usar **bandas 3 e 28** no Brasil.  
- O modem pode levar até **60 segundos** para registrar na rede NB‑IoT.  
