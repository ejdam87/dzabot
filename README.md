# Dzabot - Mobile Robot with Remote Control

This repository contains a firmware for my hand-crafted robot together with the controller.

## Components

| Component | Purpose |
|---|---|
| ESP32-S3 with OV5640 camera module | Main computing unit on the robot |
| DRV8833 motor driver | Controlling 2 DC motors (rear-wheel drive) |
| 5x electrolytic capacitor 100µF | Smoothing out the WiFi transmission current requirements |
| 4x 1.5V batteries + battery pack | Power supply for both the MCU and the driver |
| USB Power Bank | Power supply for the ESP32-C3 |
| Analog joystick | Input device for the robot control |
| ESP32-C3 | Computing unit in the wireless controller |
| Jumper wires + solderless breadboard | Wiring and physical/electrical connections between the components |

## Software Libraries

I used Arduino Core libraries and related compilers to create the firmware for both the robot and the controller.

Specific included libraries:

- `esp_now.h`
- `esp_wifi.h`
- `Wifi.h`
- `WebServer.h`
- `esp_camera.h`
- `WiFiClientSecure.h`
- `HTTPClient.h`
- `camera_pin_config.hpp`

## Functionality

The robot MCU (ESP32-S3) communicates with the controller MCU (ESP32-C3) via ESP-NOW protocol (based on WiFi physical stack). Moreover, the robot MCU has a server (on it's own access point - you need to connect with your client device to it) listening at `http://192.168.4.1/` which on `GET /` or `GET /photo` request captures a photo with the camera and serves it to the client.

## Abstract Schematic

I do not include exact pin wiring since I do not think it's important - the wiring is fairly simple for both the robot and the controller. Feel free to change the pins as you need (they are always defined at the top of the source code) - just keep in mind to avoid collisions with pins used by the camera. Also, place the capacitors across the power supply lines (+ / -) to smooth out the current demand bursts caused by WiFi transmission. I used 5x100μF electrolytic capacitors in parallel to gain higher total capacitance. If you have 1 bigger capacitor (like 1000μF), feel free to use that instead. I also power both the MCU on the robot and motors from a single power supply (in parallel). You can also use separate power supplies (with the common ground).

![Abstract Schematic](./images/dzabot_abstract_schematic.png)

## Photo(s)

### Robot

![Robot](./images/vehlicle.jpg)

### Controller

![Controller](./images/controller.jpg)
