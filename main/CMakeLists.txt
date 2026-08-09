idf_component_register(SRCS "adif_log.cpp" "network_time.cpp" "app_settings.cpp" "qso_model.cpp" "main.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_netif esp_wifi nvs_flash)
