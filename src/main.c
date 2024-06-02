#include "stdio.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi_types.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "stdint.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define WIFI_SSID "SCS-WLAN-MA"
#define WIFI_PASS "1Zurx7O0MU0F"
#define PORT 80



static const char *TAG = "ESP32_SERVER";

static bool is_event_loop_created = false;

void init_gpio(gpio_num_t freePin, gpio_num_t stoppedPin, gpio_num_t underRepairPin) {
    gpio_config_t io_conf;
    
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << freePin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << stoppedPin);
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << underRepairPin);
    gpio_config(&io_conf);
}

void handle_json_data(const char *json_data, const char *part_no, gpio_num_t freePin, gpio_num_t stoppedPin, gpio_num_t UnderRepair) {
    // Parse JSON data
    cJSON *root = cJSON_Parse(json_data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON data");
        return;
    }

    // Navigate to the "Injection plastique" object
    cJSON *injection_plastique = cJSON_GetObjectItem(root, "Injection plastique ");
    if (injection_plastique == NULL) {
        ESP_LOGE(TAG, "Failed to find 'Injection plastique' in JSON data");
        cJSON_Delete(root);
        return;
    }

    // Navigate to the specific part number
    cJSON *part = cJSON_GetObjectItem(injection_plastique, part_no);
    if (part == NULL) {
        ESP_LOGE(TAG, "Failed to find part number '%s' in JSON data", part_no);
        cJSON_Delete(root);
        return;
    }

    // Get the "Status" field for the specific part number
    cJSON *status = cJSON_GetObjectItem(part, "Status");
    if (status == NULL || !cJSON_IsString(status)) {
        ESP_LOGE(TAG, "Failed to find 'Status' for part number '%s' in JSON data", part_no);
        cJSON_Delete(root);
        return;
    }
    init_gpio(freePin, stoppedPin, UnderRepair);

    // Set GPIO pins based on the status
    if (strcmp(status->valuestring, "Free") == 0) {
        gpio_set_level(freePin, 1);
        gpio_set_level(stoppedPin, 0);
        gpio_set_level(UnderRepair, 0);
        ESP_LOGI(TAG, "Status is Free: GPIO %d set high", freePin);
    } else if (strcmp(status->valuestring, "Stopped") == 0) {
        gpio_set_level(freePin, 0);
        gpio_set_level(stoppedPin, 1);
        gpio_set_level(UnderRepair, 0);
        ESP_LOGI(TAG, "Status is Blocked: GPIO %d set high", stoppedPin);
    } else if (strcmp(status->valuestring, "Under repair") == 0) {
        gpio_set_level(freePin, 0);
        gpio_set_level(stoppedPin, 0);
        gpio_set_level(UnderRepair, 1);
        ESP_LOGI(TAG, "Status is UnderRepair : GPIO %d set high", UnderRepair);
    } 
    else {
        gpio_set_level(freePin, 0);
        gpio_set_level(stoppedPin, 0);
        gpio_set_level(UnderRepair, 0);

        ESP_LOGI(TAG, "Status is neither Free nor Blocked: both GPIOs set low");
    }

    cJSON_Delete(root);
}


// HTTP POST request handler
esp_err_t post_handler(httpd_req_t *req) {
    char buf[100];
    int ret, remaining = req->content_len;

    // Allocate memory for body buffer
    char* body = (char*)malloc(remaining + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Read body data
    char* body_ptr = body; // Use a pointer to the start of the body buffer
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(body);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        memcpy(body_ptr, buf, ret);
        body_ptr += ret;
        remaining -= ret;
    }
    *body_ptr = '\0'; // Null-terminate the body string

    // Process received JSON data
    ESP_LOGI(TAG, "Received JSON data: %s", body);
    handle_json_data(body,"01 041 335 20", 12, 14, 16);
    handle_json_data(body,"123 501 00 00", 18, 20, 22);


    
    // Send response
    httpd_resp_send(req, "Data received successfully", HTTPD_RESP_USE_STRLEN);

    // Free memory
    free(body);
    
    return ESP_OK;
}

// HTTP server initialization
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handler for POST requests
        httpd_uri_t post_uri = {
            .uri       = "/receive_data",
            .method    = HTTP_POST,
            .handler   = post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &post_uri);
        ESP_LOGI(TAG, "HTTP server started on port: %d", config.server_port);
        return server;
    }

    ESP_LOGI(TAG, "Failed to start HTTP server");
    return NULL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip_str[IP4ADDR_STRLEN_MAX]; // Buffer to hold the IP address string
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, IP4ADDR_STRLEN_MAX);
        ESP_LOGI(TAG, "Got IP: %s", ip_str);
        start_webserver();
    }
}

// Wi-Fi initialization
void wifi_init(void) {
    ESP_LOGI(TAG, "Starting...");

    if (!is_event_loop_created) {
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        is_event_loop_created = true;  // Set the flag to true after creating the event loop
    }
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_netif_init();  // Initialize network interface
    wifi_init();
}
