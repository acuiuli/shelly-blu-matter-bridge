idf_component_register(SRCS "app_main.cpp" PRIV_INCLUDE_DIRS "." REQUIRES esp_matter bridge_core nvs_flash app_update console esp_timer openthread esp_wifi)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
