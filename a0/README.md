# A0 Base Board

**A0** is the base board of the modular A0 IoT gateway, built around an **ESP32-C6 module** (RISC-V, Wi-Fi 6, Bluetooth 5, IEEE 802.15.4).

## About the ESP32-C6

The ESP32-C6 is Espressif's first Wi-Fi 6 SoC, integrating **2.4 GHz Wi-Fi 6 (802.11ax)**, **Bluetooth 5 (LE)** and **IEEE 802.15.4** (Thread/Zigbee) radio connectivity in a single low-power chip ([product page](https://www.espressif.com/en/products/socs/esp32-c6)).

| Spec            | Details                                                                                                                                                                                                                                                        |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **CPU**         | High-performance 32-bit RISC-V core, up to **160 MHz**, plus a low-power (LP) 32-bit RISC-V core, up to **20 MHz**                                                                                                                                             |
| **Memory**      | 320 KB ROM, 512 KB SRAM, external flash                                                                                                                                                                                                                        |
| **Wireless**    | 2.4 GHz Wi-Fi 6 (802.11ax, 20 MHz) with 802.11 b/g/n (20/40 MHz) backward compatibility; OFDMA up/downlink, downlink MU-MIMO, Target Wake Time (TWT); Bluetooth 5 LE with coded PHY long-range and 2 Mbps high-throughput PHY; IEEE 802.15.4 for Thread/Zigbee |
| **GPIO**        | 30 (QFN40) or 22 (QFN32) programmable GPIOs                                                                                                                                                                                                                    |
| **Peripherals** | SPI, UART, I2C, I2S, RMT, TWAI, PWM, SDIO, Motor Control PWM, 12-bit ADC, temperature sensor                                                                                                                                                                   |
| **Security**    | RSA-3072 secure boot, AES-128/256-XTS flash encryption, digital signature and HMAC peripherals, hardware cryptographic accelerators, Trusted Execution Environment (TEE) for privilege/software separation                                                     |
| **Software**    | Open-source ESP-IDF; ESP-Hosted / ESP-AT for co-processor use; ESP RainMaker; Zephyr support                                                                                                                                                                   |

Wi-Fi 6 features (OFDMA, MU-MIMO, TWT) deliver low latency and long battery life even in congested RF environments, while combined Wi-Fi + BLE + 802.15.4 support makes it a natural fit for Matter end-point devices, Thread Border Routers, and Zigbee bridges.

## Overview

A0 provides everything needed to power and run the gateway, while pushing all radio/cellular connectivity onto removable expansion modules:

- **ESP32-C6 module on top** — main compute + Wi-Fi/BLE/802.15.4 connectivity, with hardware secure boot, flash encryption, and a TEE.
- **Power**:
  - USB-C power input
  - External power supply input
  - Battery support (with charging)
- **Expansion**: two module slots, **BUS1** and **BUS2**, where any of the following modules can be plugged in:
  - [SIM7080G](../modules/sim7080g/README.md) — NB-IoT/LTE-M cellular + GNSS
  - [WMBUS](../modules/wmbus/README.md) — Wireless M-Bus radio
  - [E07-400M10S](../modules/E07400M10S/README.md) — Sub-GHz radio (400 MHz, 10 dBm)

## Manufacturing

Gerber and assembly outputs are in [`jlcpcb/`](jlcpcb/) (`gerber/` and `production_files/`).
