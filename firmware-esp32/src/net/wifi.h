#pragma once

// Starts WiFi: connects to the stored network, or falls back to a SoftAP
// ("ReflowOven") if no credentials are configured.
void wifi_init(void);