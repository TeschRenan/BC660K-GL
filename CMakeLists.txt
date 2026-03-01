set(srcs "src/bc660k.cpp")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS include
                    PRIV_INCLUDE_DIRS ""
                    REQUIRES esp_driver_uart esp_driver_gpio)