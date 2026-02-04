# OmniaPi Node Mesh - ESP-WIFI-MESH Node Firmware
# Target: ESP32-C3

cmake_minimum_required(VERSION 3.16)

# Set target before including project.cmake
set(IDF_TARGET "esp32c3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(omniapi_node_mesh)
