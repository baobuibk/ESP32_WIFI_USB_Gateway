set(priv_requires fatfs console "FTP" "SD_Card" "wifi" "nvs")
list(APPEND priv_requires wear_levelling esp_partition)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS .
    PRIV_REQUIRES "${priv_requires}"
)
