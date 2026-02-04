# OmniaPi Gateway Mesh - ESP-IDF Project
# Target: ESP32 (WT32-ETH01)
# Features: ESP-WIFI-MESH (Fixed Root), Ethernet, MQTT, OTA

cmake_minimum_required(VERSION 3.16)

# Set target before project()
set(IDF_TARGET "esp32")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(omniapi_gateway_mesh)
