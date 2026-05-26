idf_component_register(
    SRCS
        "src/esp_audio_pcm.c"
        "src/esp_audio_pcm_usb.c"
        "src/esp_audio_pcm_uart.c"
        "src/esp_audio_pcm_ctrl.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_usb_serial_jtag
        esp_driver_uart
)
