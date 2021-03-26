//#define LOG_LOCAL_LEVEL ESP_LOG_ERROR

#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "rom/ets_sys.h"
#include <string.h>

#include "ota.h"

#include "mqtt_client.h"
#include "remote_log.h"

//#define DEBUG_SERIAL

static const char *TAG = "beamer-control";

#include "beamer.h"
#include "homie.h"

#define STR_(X) #X
#define STR(X) STR_(X)

static EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t mqtt_event_group;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static int OTA_ongoing = HOMIE_FALSE;

const static int MQTT_NEW_CONNECT_BIT = BIT0;
const static int MQTT_CONNECTED_BIT = BIT1;

void start_ota(struct homie_handle_s *handle, int node,
                                int property, const char *data, int data_len);

homie_handle_t homie = {
    .deviceid = "beamer-control",
    .devicename = "Beamer Control",
    .update_interval =
        0, /* set to 0 to workaround openhab problem of taking device offline */
    .firmware = "foo",
    .firmware_version = "bar",
    //       .firmware = GIT_URL,
    //       .firmware_version = GIT_BRANCH" "GIT_COMMIT_HASH,
    .num_nodes = 1,
    .nodes = {{.id = "epson-beamer",
               .name = "Epson Beamer",
               .type = "TW3600",
               .num_properties = 3,
               .properties =
                   {
                       {
                           .id = "power",
                           .name = "Power",
                           .settable = HOMIE_TRUE,
                           .retained = HOMIE_TRUE,
                           .unit = " ",
                           .datatype = HOMIE_BOOL,
                           .read_property_cbk = &update_power,
                           .write_property_cbk = &write_power,
                           .user_data = &beamer_state,
                       },
                       {
                           .id = "source",
                           .name = "source",
                           .settable = HOMIE_TRUE,
                           .retained = HOMIE_TRUE,
                           .unit = " ",
                           .datatype = HOMIE_INTEGER,
                           .read_property_cbk = &update_source,
                           .write_property_cbk = &write_source,
                           .user_data = &beamer_state,
                       },
                       {
                           .id = "update",
                           .name = "update",
                           .settable = HOMIE_TRUE,
                           .retained = HOMIE_TRUE,
                           .unit = " ",
                           .datatype = HOMIE_BOOL,
                           .read_property_cbk = NULL,
                           .write_property_cbk = &start_ota,
                           .user_data = NULL,
                       },
                   }}},
    .uptime = 0,
};

int wifi_retry_count = 0;
const int WIFI_CONNECTED_BIT = BIT0;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        xEventGroupSetBits(mqtt_event_group, MQTT_NEW_CONNECT_BIT);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        xEventGroupClearBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        xEventGroupClearBits(mqtt_event_group, MQTT_NEW_CONNECT_BIT);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "MQTT_EVENT_DATA, topic %*.s", event->topic_len, event->topic);
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA='%.*s'\r\n", event->data_len, event->data);
        printf("ID=%d, total_len=%d, data_len=%d, current_data_offset=%d\n",
               event->msg_id, event->total_data_len, event->data_len,
               event->current_data_offset);

        homie_handle_mqtt_incoming_event(&homie, event);

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGD(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_app_start(void)
{
    char uri[256];
    sprintf(uri, "mqtt://%s:%s@%s:%d", CONFIG_MQTT_USER, CONFIG_MQTT_PASSWORD,
            CONFIG_MQTT_SERVER, CONFIG_MQTT_PORT);
    mqtt_event_group = xEventGroupCreate();
    const esp_mqtt_client_config_t mqtt_cfg = {
        .event_handle = mqtt_event_handler,
        .uri = uri,
    };
    xEventGroupClearBits(mqtt_event_group, MQTT_CONNECTED_BIT);

    ESP_LOGI(TAG, "connect to mqtt uri %s", uri);
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_LOGI(TAG, "Note free memory: %d bytes", esp_get_free_heap_size());
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START: {
            esp_wifi_connect();
            esp_err_t err;
            char hostname[33] = {0};
            snprintf(hostname, 33, "beamer-control");
            ESP_LOGI(TAG, "set hostname to %s", hostname);
            if ((err = tcpip_adapter_set_hostname(WIFI_IF_STA, hostname)) != ESP_OK)
            {
                ESP_LOGE(TAG, "set hostname failed: %s", esp_err_to_name(err));
            }
        }
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        snprintf(homie.ip, sizeof(homie.ip), "%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

        start_remote_log(CONFIG_REMOTELOG_UDP_HOST, CONFIG_REMOTELOG_UDP_PORT,
            CONFIG_REMOTELOG_SYSLOG_HOST, CONFIG_REMOTELOG_SYSLOG_PORT, CONFIG_REMOTELOG_SYSLOG_APP);
        esp_mqtt_client_start(mqtt_client);

        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station:" MACSTR " join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station:" MACSTR "leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT)
        {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B |
                                                       WIFI_PROTOCOL_11G |
                                                       WIFI_PROTOCOL_11N);
        }
        stop_remote_log();
        esp_mqtt_client_stop(mqtt_client);

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_retry_count++;
        ESP_LOGI(TAG, "retry to connect to the AP");
        if (wifi_retry_count > 10)
        {
            ESP_LOGI(TAG, "reboot to many tries");
            fflush(stdout);
            esp_restart();
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

void connect_to_wifi()
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {.ssid = CONFIG_WIFI_SSID, .password = CONFIG_WIFI_PASSWORD},
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(homie.mac, sizeof(homie.mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             eth_mac[0], eth_mac[1], eth_mac[2], eth_mac[3], eth_mac[4],
             eth_mac[5]);

    ESP_LOGI(TAG, "homie device id %s", homie.deviceid);
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", CONFIG_WIFI_SSID,
             CONFIG_WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_start());
}

void start_ota(struct homie_handle_s *handle, int node, int property, const char *data, int data_len)
{
    if (data_len > 0)
    {
        char buf_topic[255] = {0};
        char url[255] = {0};

        snprintf(url, sizeof(url), "%.*s", data_len, data);
        
        OTA_ongoing = HOMIE_TRUE;
        ESP_LOGI(TAG, "get OTA update from %s", url);

        esp_err_t ret = execute_ota(url);

        // reset update topic to prevent inifinte update loop
        sprintf(buf_topic, "homie/%s/%s/update/set", homie.deviceid, homie.nodes[0].id);
        esp_mqtt_client_publish(homie.mqtt_client, buf_topic, "",
                            0, 1, HOMIE_TRUE);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "reset to start new image");
            fflush(stdout);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        }
        OTA_ongoing = HOMIE_FALSE;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "starting....\n");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    beamer_state.mutex = xSemaphoreCreateMutex();

    connect_to_wifi();

    const esp_partition_t* current_partition = esp_ota_get_running_partition();
    strncpy(homie.firmware, current_partition->label, sizeof(homie.firmware));

    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    strncpy(homie.firmware_version, app_desc->version, sizeof(homie.firmware_version));
    ESP_LOGI(TAG, "running partition %s version %s", current_partition->label, app_desc->version);
    mqtt_app_start();
    homie.mqtt_client = mqtt_client;

    ESP_LOGI(TAG, "uart init");
    #ifndef DEBUG_SERIAL
    uart_init(&uart);
    #endif

    for (int i = 24 * 60 * 60 / 5; i >= 0; i--)
    {
        EventBits_t uxBits;
        ESP_LOGD(TAG, "Restarting in %d seconds...\n", i * 5);

        homie.uptime += 5;

        ESP_LOGD(TAG, "test for mqtt new connect");
        uxBits = xEventGroupWaitBits(mqtt_event_group, MQTT_NEW_CONNECT_BIT, true,
                                     false, 1);
        if ((uxBits & MQTT_NEW_CONNECT_BIT) != 0)
        {
            ESP_LOGI(TAG, "homie init");
            homie_init(&homie);
            ESP_LOGI(TAG, "homie init done");
        }
        
        uxBits = xEventGroupWaitBits(mqtt_event_group, MQTT_CONNECTED_BIT, false,
                                     false, 1);
        if ((uxBits & MQTT_CONNECTED_BIT) != 0)
        {
            ESP_LOGI(TAG, "homie cycle");
            #ifndef DEBUG_SERIAL
            homie_cycle(&homie);
            #endif
        }
        #ifdef DEBUG_SERIAL
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        #else
        uart_cycle(&uart); // is waiting 5 sec
        #endif
    }
    ESP_LOGI(TAG, "Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
