idf_component_register(SRCS "basic_example.c" "board.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_audio_pcm_toolkit hi_nomi_board esp_codec_dev)
