set(component_srcs "ftp.c")

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES "freertos" SD_Card
                       )