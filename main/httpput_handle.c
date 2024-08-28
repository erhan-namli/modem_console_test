#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_err.h"

static const char *TAG = "modem_console_httpput";

extern const uint8_t _binary_cert_pem_start[] asm("_binary_cert_pem_start");
extern const uint8_t _binary_cert_pem_end[] asm("_binary_cert_pem_end");

static esp_err_t init_global_ca_store(void)
{
    const size_t ca_pem_bytes = _binary_cert_pem_end - _binary_cert_pem_start;

    esp_err_t ret = esp_tls_init_global_ca_store();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize global CA store");
        return ret;
    }

    ret = esp_tls_set_global_ca_store(_binary_cert_pem_start, ca_pem_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set global CA store");
        return ret;
    }

    return ESP_OK;
}

static esp_err_t http_put_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_PUT_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_PUT_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_PUT_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_PUT_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_PUT_EVENT_ON_DATA, len=%d", evt->data_len);
        if ((bool)evt->user_data &&
                !esp_http_client_is_chunked_response(evt->client)) {
            ESP_LOG_BUFFER_HEXDUMP(TAG, evt->data, evt->data_len, ESP_LOG_INFO);
        }

        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_PUT_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_PUT_EVENT_DISCONNECTED");
        break;
    default: break;
    }
    return ESP_OK;
}

static struct {
    struct arg_str *host;
    struct arg_lit *hex;
    struct arg_end *end;
} http_args;

static int do_http_put_client(int argc, char **argv)
{
    esp_err_t ret = init_global_ca_store();
    if (ret != ESP_OK) {
        return 1;
    }
    
    int nerrors = arg_parse(argc, argv, (void **)&http_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, http_args.end, argv[0]);
        return 1;
    }
    
    esp_http_client_config_t config = {
        .url = http_args.host->count > 0 ? http_args.host->sval[0] : "https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/put-data",
        .event_handler = http_put_event_handler,
        .use_global_ca_store = true,
        .method = HTTP_METHOD_PUT,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    const char *json_data = "{\"device\": \"ESP32\", \"status\": \"active\"}";
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_data, strlen(json_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        uint64_t content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP PUT Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client), content_length);
        esp_http_client_cleanup(client);
        return 0;
    }
    ESP_LOGE(TAG, "HTTP PUT request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return 1;
}

void modem_console_register_http_put(void)
{
    http_args.host = arg_str0(NULL, NULL, "<host>", "address or host-name to send PUT request (defaults to https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/put-data)");
    http_args.hex = arg_litn("p", "print-hex", 0, 1, "print hex output"),
    http_args.end = arg_end(1);
    const esp_console_cmd_t http_cmd = {
        .command = "httpput",
        .help = "http put command to test data mode",
        .hint = NULL,
        .func = &do_http_put_client,
        .argtable = &http_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&http_cmd));
}

