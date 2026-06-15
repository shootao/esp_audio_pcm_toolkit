idf_component_register(SRCS "basic_example.c" "net_init.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_audio_pcm_toolkit esp_wifi esp_netif nvs_flash esp_event)
