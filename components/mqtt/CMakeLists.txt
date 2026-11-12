idf_component_register(
    SRCS "mqtt_client.c" "platform.c" "mqtt_msg.c" "mqtt_outbox.c" "mqtt_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event tcp_transport
    PRIV_REQUIRES esp_timer http_parser esp_hw_support heap
    KCONFIG "${CMAKE_CURRENT_LIST_DIR}/Kconfig"
)
