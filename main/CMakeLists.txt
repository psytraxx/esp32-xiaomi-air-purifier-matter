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
