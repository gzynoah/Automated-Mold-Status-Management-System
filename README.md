# ESP32 Automated Mold Status Management System

This project is designed to monitor and manage the status of injection mold parts using an ESP32 microcontroller. The system connects to a Wi-Fi network, hosts an HTTP server, and processes incoming JSON data to control GPIO pins based on the part status.

## Features

- Connects to a Wi-Fi network.
- Hosts an HTTP server on port 80.
- Accepts HTTP POST requests containing JSON data.
- Controls GPIO pins based on the status of specified parts.

## Hardware Requirements

- ESP32 Development Board
- GPIO pins connected to LEDs or other indicators for status display

## Software Requirements

- ESP-IDF framework
- FreeRTOS
- cJSON library for JSON parsing

## Getting Started

### Prerequisites

- Install ESP-IDF (Espressif IoT Development Framework)
- Set up the development environment as per the ESP-IDF documentation

### Project Structure

