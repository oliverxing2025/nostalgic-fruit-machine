idf_component_register(
    SRCS "main.c" "fruit_audio.c" "fruit_game_config.c"
         "fiery_dragon_image.c" "fruit_header_image.c"
         "fruit_controls_image.c" "fruit_controls_pending_image.c"
         "fruit_track_image.c" "vibe_board.c"
         "../third_party/bmi270/bmi2.c" "../third_party/bmi270/bmi270.c"
    INCLUDE_DIRS "../include" "../third_party/bmi270"
    REQUIRES driver esp_timer esp_lcd nvs_flash espressif__esp_codec_dev
             espressif__button lvgl__lvgl
)
