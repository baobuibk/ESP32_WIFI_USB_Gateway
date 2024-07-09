set(component_srcs "connect.c" "mode.c" "scan_ssid.c" "smart_config.c" "store_ssid_pass.c")

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS "inc"
                       PRIV_REQUIRES "freertos" "lwip" "esp_wifi" nvs 
                       )