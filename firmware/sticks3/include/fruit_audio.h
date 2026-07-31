#pragma once

#include "esp_err.h"

typedef enum {
    FRUIT_SOUND_TICK,
    FRUIT_SOUND_START,
    FRUIT_SOUND_WIN,
    FRUIT_SOUND_WIN_LOW,
    FRUIT_SOUND_WIN_MEDIUM,
    FRUIT_SOUND_WIN_HIGH,
    FRUIT_SOUND_WIN_TOP,
    FRUIT_SOUND_BONUS,
    FRUIT_SOUND_LOSE,
    FRUIT_SOUND_JACKPOT,
    FRUIT_SOUND_BIG_BANG,
} fruit_sound_t;

esp_err_t fruit_audio_init(void);
esp_err_t fruit_audio_play(fruit_sound_t sound);
