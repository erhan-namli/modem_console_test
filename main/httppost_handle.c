/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/*
 * Modem console example with file upload functionality
 *
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  // Include FreeRTOS header for delay
#include "esp_heap_caps.h"  // Include this header for heap_caps_malloc
#include "esp_log.h"  // Include for esp_log_timestamp

static const char *TAG = "modem_console_httppost";

extern const uint8_t _binary_cert_pem_start[] asm("_binary_cert_pem_start");
extern const uint8_t _binary_cert_pem_end[] asm("_binary_cert_pem_end");

// Declare http_args globally so it can be used in both functions
static struct {
    struct arg_str *host;
    struct arg_str *file;
    struct arg_end *end;
} http_args;

static esp_err_t init_global_ca_store(void)
{
    const size_t ca_pem_bytes = _binary_cert_pem_end - _binary_cert_pem_start;

    esp_err_t ret = esp_tls_init_global_ca_store();
    if (ret != ESP_OK) {
        printf("Failed to initialize global CA store\n");
        return ret;
    }

    ret = esp_tls_set_global_ca_store(_binary_cert_pem_start, ca_pem_bytes);
    if (ret != ESP_OK) {
        printf("Failed to set global CA store\n");
        return ret;
    }

    return ESP_OK;
}

static esp_err_t http_post_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        printf("HTTP_EVENT_ERROR\n");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        printf("HTTP_EVENT_ON_CONNECTED\n");
        break;
    case HTTP_EVENT_HEADER_SENT:
        printf("HTTP_EVENT_HEADER_SENT\n");
        break;
    case HTTP_EVENT_ON_HEADER:
        printf("HTTP_EVENT_ON_HEADER, key=%s, value=%s\n", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        printf("HTTP_EVENT_ON_DATA, len=%d\n", evt->data_len);
        if ((bool)evt->user_data && !esp_http_client_is_chunked_response(evt->client)) {
            printf("HTTP data received\n");
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        printf("HTTP_EVENT_ON_FINISH\n");
        break;
    case HTTP_EVENT_DISCONNECTED:
        printf("HTTP_EVENT_DISCONNECTED\n");
        break;
    default:
        break;
    }
    return ESP_OK;
}

static char *read_file_to_buffer(const char *file_path, long *file_size)
{
    printf("Attempting to read file: %s\n", file_path);

    FILE *file = fopen(file_path, "rb");
    if (!file) {
        printf("Failed to open file for reading\n");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("File size: %ld bytes\n", *file_size);

    // Allocate memory in PSRAM
    char *file_buffer = heap_caps_malloc(*file_size, MALLOC_CAP_SPIRAM);
    if (!file_buffer) {
        printf("Failed to allocate memory in PSRAM for file\n");
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(file_buffer, 1, *file_size, file);
    fclose(file);

    if (read_size != *file_size) {
        printf("Failed to read the entire file. Read %zu bytes, expected %ld bytes\n", read_size, *file_size);
        free(file_buffer);
        return NULL;
    }

    printf("File read successfully\n");

    return file_buffer;
}


// Modified to use a smaller chunk size and increased timeout

static esp_err_t upload_file_to_server(esp_http_client_handle_t client, const char *file_buffer, long file_size, const char *file_name) {
    printf("Preparing to upload file: %s\n", file_name);
    
    const char boundary[] = "----WebKitFormBoundary7MA4YWxkTrZu0gW"; // Arbitrary boundary string
    char buffer[512]; // Buffer for the multipart headers

    int len = snprintf(buffer, sizeof(buffer),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n",
        boundary, file_name);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    char content_type_header[128];
    snprintf(content_type_header, sizeof(content_type_header), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type_header);

    printf("Opening HTTP connection\n");
    esp_err_t err = esp_http_client_open(client, file_size + len + strlen(boundary) + 8); // +8 for additional boundaries and newlines
    if (err != ESP_OK) {
        printf("Failed to open HTTP connection: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("Writing multipart form header\n");
    err = esp_http_client_write(client, buffer, len);
    if (err < 0) {
        printf("Failed to write multipart form header\n");
        return err;
    }

    // Track time and bytes sent for upload speed calculation
    int64_t start_time = esp_log_timestamp();
    size_t total_bytes_sent = 0;

    const size_t chunk_size = 4096; // Reduced chunk size
    for (size_t i = 0; i < file_size; i += chunk_size) {
        size_t bytes_to_write = chunk_size;
        if (i + chunk_size > file_size) {
            bytes_to_write = file_size - i;
        }

        err = esp_http_client_write(client, file_buffer + i, bytes_to_write);
        if (err < 0) {
            printf("Failed to write file content chunk\n");
            return err;
        }

        total_bytes_sent += bytes_to_write;

        // Calculate time elapsed and upload speed
        int64_t elapsed_time = esp_log_timestamp() - start_time; // Time in milliseconds
        double elapsed_seconds = elapsed_time / 1000.0; // Convert to seconds
        double upload_speed = total_bytes_sent / elapsed_seconds / 1024.0; // Speed in KB/s

        printf("Uploaded %zu/%ld bytes (%.2f%%) at %.2f KB/s\n",
               total_bytes_sent, file_size, (total_bytes_sent / (double)file_size) * 100, upload_speed);
    }

    printf("Writing closing boundary\n");
    len = snprintf(buffer, sizeof(buffer), "\r\n--%s--\r\n", boundary);
    err = esp_http_client_write(client, buffer, len);
    if (err < 0) {
        printf("Failed to write closing boundary\n");
        return err;
    }

    printf("Performing HTTP request\n");
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        uint64_t content_length = esp_http_client_get_content_length(client);
        printf("File uploaded successfully, status = %d, content_length = %llu\n",
               esp_http_client_get_status_code(client), content_length);
    } else {
        printf("File upload failed: %s\n", esp_err_to_name(err));
    }

    return err;
}



static int do_http_post_client(int argc, char **argv)
{
    printf("Starting HTTP POST Client\n");

    esp_err_t ret = init_global_ca_store();
    if (ret != ESP_OK) {
        printf("Failed to initialize global CA store\n");
        return 1;
    }
    
    int nerrors = arg_parse(argc, argv, (void **)&http_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, http_args.end, argv[0]);
        return 1;
    }

    printf("Creating HTTP client config\n");
    esp_http_client_config_t config = {
    .url = http_args.host->count > 0 ? http_args.host->sval[0] : "https://app-esp32-modem-test-8d3ed92fa208.herokuapp.com/upload",
    .event_handler = http_post_event_handler,
    .use_global_ca_store = true,
    .cert_pem = NULL, // Disable certificate verification temporarily
    .timeout_ms = 10000, // Increase the timeout to 10 seconds
};

esp_http_client_handle_t client = esp_http_client_init(&config);

// Add connection close header
esp_http_client_set_header(client, "Connection", "close");

    // Read file into buffer
    printf("Reading file: %s\n", http_args.file->sval[0]);
    long file_size;
    char *file_buffer = read_file_to_buffer(http_args.file->sval[0], &file_size);
    if (!file_buffer) {
        printf("Failed to read file into buffer\n");
        return 1;
    }

    // Extract the file name from the path
    const char *file_name = strrchr(http_args.file->sval[0], '/');
    if (!file_name) {
        file_name = http_args.file->sval[0]; // No directory in path
    } else {
        file_name++; // Skip the '/'
    }

    printf("File name extracted: %s\n", file_name);

    // Upload file to server
    printf("Uploading file to server\n");
    esp_err_t err = upload_file_to_server(client, file_buffer, file_size, file_name);

    free(file_buffer);
    esp_http_client_cleanup(client);

    return err == ESP_OK ? 0 : 1;
}

void modem_console_register_http_post(void)
{
    http_args.host = arg_str0(NULL, NULL, "<host>", "address or host-name to send POST request (defaults to https://<your-heroku-app-name>.herokuapp.com/upload)");
    http_args.file = arg_str1(NULL, NULL, "<file>", "file to upload");
    http_args.end = arg_end(1);
    const esp_console_cmd_t http_cmd = {
        .command = "httppost",
        .help = "http post command to upload a file",
        .hint = NULL,
        .func = &do_http_post_client,
        .argtable = &http_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&http_cmd));
}