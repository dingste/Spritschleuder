#pragma once

#include "esp_err.h"

namespace LocalDiscovery {
constexpr const char *HOSTNAME = "spritschleuder";
constexpr const char *URL = "http://spritschleuder.local";

esp_err_t start();
void stop();
bool running();
}  // namespace LocalDiscovery
