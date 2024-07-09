set(component_srcs "sd_card.c")

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS "."
                       REQUIRES "driver" "sdmmc" "fatfs"
                       )