#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "storage/config.h"

#define TAG "wifi"

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect(); // keep retrying
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();

    char ssid[33] = "";
    char pass[65] = "";
    config_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));

    if (ssid[0]) {
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&wc);
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
        wifi_config_t cfg = { 0 };
        strncpy((char *)cfg.sta.ssid, ssid, 32);
        strncpy((char *)cfg.sta.password, pass, 64);
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        esp_wifi_start();
        ESP_LOGI(TAG, "connecting to %s", ssid);
    } else {
        esp_netif_create_default_wifi_ap();
        wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&wc);
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
        wifi_config_t cfg = { 0 };
        strncpy((char *)cfg.ap.ssid, "ReflowOven", 32);
        cfg.ap.ssid_len = strlen("ReflowOven");
        cfg.ap.channel = 1;
        cfg.ap.max_connection = 4;
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy((char *)cfg.ap.password, "reflow123", 64);
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_set_config(WIFI_IF_AP, &cfg);
        esp_wifi_start();
        ESP_LOGI(TAG, "SoftAP 'ReflowOven' (192.168.4.1)");
    }
}