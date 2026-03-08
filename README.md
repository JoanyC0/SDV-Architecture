# SDV-Architecture

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Table of Contents
- [Overview](#overview)
- [Architecture](#architecture)
- [Components](#components)
- [Features](#features)
- [OTA Workflow](#ota-workflow)
- [Getting Started](#getting-started)
- [License](#license)
- [Acknowledgements](#acknowledgements)

---

## Overview

`sdv-architecture` is a modular and secure OTA firmware update system for **STM32H743VIT6** using an **ESP32 OTA server** as an interface, managed from a remote Linux-based calculator.  

It is designed for **industrial embedded systems**, enabling remote firmware updates over the Internet without interfering with the real-time execution on the STM32.

---

## Architecture

### Component Roles

- **Linux Calculator / Remote Server**  
  - Generates firmware or control commands  
  - Sends firmware securely over Internet to ESP32 OTA server  

- **ESP32 OTA Server**  
  - Web-based and Internet-accessible firmware uploader  
  - Receives firmware from remote Linux calculator  
  - Transmits firmware in reliable packets over UART to STM32  
  - Handles ACK, retries, erase commands, and OTA logs  

- **STM32H743VIT6 MCU**  
  - Bootloader with **dual-slot firmware**  
  - Micro-ROS client for real-time control  
  - Receives OTA packets via UART from ESP32  
  - Executes primary real-time application (PCU)

---

## Features

- Dual-slot OTA on STM32H743 for **safe updates and rollback**  
- Internet-enabled OTA via ESP32 server  
- Reliable packet-based UART transfer with **ACK and retry**  
- OTA progress monitoring and logging  
- STM32 bootloader integration with micro-ROS client  

---

## OTA Workflow

1. **Firmware generation** on Linux calculator (build scripts or tools).  
2. **Upload** firmware securely to ESP32 OTA server via Internet.  
3. ESP32 **erases target slot** on STM32 and sends firmware packets via UART.  
4. STM32 bootloader **verifies each packet** (ACK/CRC) and writes to Flash.  
5. Once complete, STM32 switches boot slot and **resets**.  
6. Micro-ROS client runs the updated firmware on STM32.  

> Dual-slot ensures **rollback** in case of update failure.

---

## Getting Started

### Requirements

- **STM32H743VIT6 MCU**  
- **ESP32 (DevKit/WROOM/WROVER)**  
- **Linux Calculator / Remote Server** with Internet connection  
- UART connection between ESP32 and STM32  
- Arduino IDE or PlatformIO for ESP32 firmware  

### Installation

1. Flash the **STM32 bootloader + micro-ROS client**.  
2. Upload **ESP32 OTA server firmware**.  
3. Connect ESP32 to STM32 via UART.  
4. Access the ESP32 OTA web interface for firmware uploads over Internet.  

### Usage

1. Open the ESP32 web interface in a browser.  
2. Select a `.bin` firmware file generated from Linux calculator.  
3. Click **Upload and Update**.  
4. Monitor OTA progress via web UI and serial logs.  
5. After success, STM32 automatically resets and boots the new firmware.  

---

## License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

## Acknowledgements

- [Espressif ESP32](https://www.espressif.com/) – OTA & Wi-Fi stack  
- [STMicroelectronics STM32H7](https://www.st.com/) – Dual-slot bootloader & micro-ROS  
- Open-source OTA frameworks and examples for STM32 and ESP32  
