
idf_component_register(SRCS "modem_console_main.cpp"
                            "console_helper.cpp"
                            "my_module_dce.cpp"
                            "httpget_handle.c"
                            "httpput_handle.c"
                            "httppost_handle.c"
                            "ping_handle.c"
                       REQUIRES console esp_http_client nvs_flash esp-tls esp_http_client littlefs
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "cert.pem")
                       

