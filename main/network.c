#include "network.h"
#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "mdns.h"
#include "star_tracker_config.h"

static const char* TAG = "Network";

// -- Private Functions --------------------------------------------------------
static void wifi_event_handler(
    void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event =
            (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(
            TAG,
            "station " MACSTR " join, AID=%d",
            MAC2STR(event->mac),
            event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t* event =
            (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(
            TAG,
            "station " MACSTR " leave, AID=%d",
            MAC2STR(event->mac),
            event->aid);
    }
}

// -- Public Functions ---------------------------------------------------------
void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* netif_handle = esp_netif_create_default_wifi_ap();

    wifi_init_config_t default_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&default_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = { .ap = { .ssid = SSID,
                                       .ssid_len = strlen(SSID),
                                       .channel = CHANNEL,
                                       .password = PASSWORD,
                                       .max_connection = MAX_CONNECTIONS,
                                       .authmode = WIFI_AUTH_WPA2_PSK,
                                       .pmf_cfg = { .required = true } } };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(netif_handle, &ip_info));
    ESP_LOGI(
        TAG,
        "Initialised wifi successfully. SSID: %s, Password: %s, IP: " IPSTR,
        SSID,
        PASSWORD,
        IP2STR(&ip_info.ip));
}

void mdns_service_init()
{
    ESP_LOGI(TAG, "Setting up mDNS");
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set(FRIENDLY_NAME));

    ESP_ERROR_CHECK(mdns_service_add(
        "StarTracker-Controller", "_http", "_tcp", 80, NULL, 0));
    ESP_ERROR_CHECK(mdns_service_subtype_add_for_host(
        "StarTracker-Controller", "_http", "_tcp", NULL, "_server"));
    ESP_LOGI(TAG, "mDNS initialised!");
}
