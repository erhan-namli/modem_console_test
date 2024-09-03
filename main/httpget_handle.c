#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_littlefs.h"

static const char *TAG = "modem_console_httpget";
static FILE *download_file = NULL;  // File pointer for downloaded file

// Define the LittleFS configuration
#define LITTLEFS_BASE_PATH "/littlefs"
// Define the path for the download directory
#define DOWNLOAD_DIR_PATH LITTLEFS_BASE_PATH "/downloads"

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

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static int64_t start_time = 0;
    static size_t total_bytes_received = 0;
    static FILE *download_file = NULL;  // File pointer for downloaded file

    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        printf("HTTP_EVENT_ERROR\n");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        printf("HTTP_EVENT_ON_CONNECTED\n");
        start_time = esp_log_timestamp();  // Start tracking time when connected
        total_bytes_received = 0;          // Reset the byte counter
        break;
    case HTTP_EVENT_HEADER_SENT:
        printf("HTTP_EVENT_HEADER_SENT\n");
        break;
    case HTTP_EVENT_ON_HEADER:
        printf("HTTP_EVENT_ON_HEADER, key=%s, value=%s\n", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0) {
            if (download_file == NULL) {
                // Open file for writing in the specified directory on LittleFS
                char file_path[128];
                snprintf(file_path, sizeof(file_path), "%s/file_downloaded.txt", DOWNLOAD_DIR_PATH);
                download_file = fopen(file_path, "w");
                if (download_file == NULL) {
                    printf("Failed to open file for writing\n");
                    return ESP_FAIL;
                }
            }

            // Write data to file
            size_t written = fwrite(evt->data, 1, evt->data_len, download_file);
            if (written != evt->data_len) {
                printf("Failed to write data to file\n");
                return ESP_FAIL;
            }

            // Update the total bytes received
            total_bytes_received += evt->data_len;

            // Calculate elapsed time and download speed
            int64_t elapsed_time = esp_log_timestamp() - start_time; // Time in milliseconds
            double elapsed_seconds = elapsed_time / 1000.0;          // Convert to seconds
            double download_speed = total_bytes_received / elapsed_seconds / 1024.0; // Speed in KB/s

            // Print download progress
            printf("Downloaded %zu bytes at %.2f KB/s\n", total_bytes_received, download_speed);
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        printf("HTTP_EVENT_ON_FINISH\n");
        if (download_file != NULL) {
            fclose(download_file);
            download_file = NULL;
            printf("File download completed and saved to %s/file_downloaded.txt\n", DOWNLOAD_DIR_PATH);
        }
        break;
    case HTTP_EVENT_DISCONNECTED:
        printf("HTTP_EVENT_DISCONNECTED\n");
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

static int do_http_client(int argc, char **argv)
{
    // Initialize the global CA store
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
        .event_handler = http_event_handler,
        .use_global_ca_store = true,
    };

    if (http_args.host->count > 0) {
        config.url = http_args.host->sval[0];
    } else {
        config.url = "https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/file/364575";
    }

    if (http_args.hex->count > 0) {
        config.user_data = (void *)true;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        uint64_t content_length = esp_http_client_get_content_length(client);
        printf("HTTP GET Status = %d, content_length = %llu\n",
               esp_http_client_get_status_code(client), content_length);
        esp_http_client_cleanup(client);
        return 0;
    }

    printf("HTTP GET request failed: %s\n", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return 1;
}

void modem_console_register_http(void)
{
    http_args.host = arg_str0(NULL, NULL, "<host>", "address or host-name to send GET request (defaults to https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/file/364575)");
    http_args.hex = arg_litn("p", "print-hex", 0, 1, "print hex output");
    http_args.end = arg_end(1);
    const esp_console_cmd_t http_cmd = {
        .command = "httpget",
        .help = "HTTP GET command to download a file",
        .hint = NULL,
        .func = &do_http_client,
        .argtable = &http_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&http_cmd));
}
