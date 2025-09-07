#pragma once

#include "esp_err.h"

// Initialize Wi-Fi (NVS, netif, event loop) and connect as STA.
// Blocks until connected or timeout_ms elapses (<=0 means wait indefinitely).
esp_err_t wifi_connect(const char* ssid, const char* password, int timeout_ms);
