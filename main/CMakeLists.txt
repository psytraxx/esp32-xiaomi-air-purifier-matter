idf_component_register(
    SRCS
        "app_main.cpp"
        "boot_diag.cpp"
        "matter_setup.cpp"
        "purifier_link.cpp"
        "miot_protocol.cpp"
        "miot_queue.cpp"
        "miot_session.cpp"
        "purifier_model.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_uart esp_netif esp_timer nvs_flash
)

# Purifier model: detected at runtime by default. Force one with
#   idf.py -DPURIFIER_MODEL=1 build   (1 = 3H, 2 = 3C)
# When unset, app_config.h supplies the default (0 = detect).
if(DEFINED PURIFIER_MODEL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "PURIFIER_MODEL=${PURIFIER_MODEL}")
endif()

# Log every MCU line on the console (bring-up / protocol captures):
#   idf.py -DPURIFIER_LINK_DEBUG=1 build
if(DEFINED PURIFIER_LINK_DEBUG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "PURIFIER_LINK_DEBUG=${PURIFIER_LINK_DEBUG}")
endif()
