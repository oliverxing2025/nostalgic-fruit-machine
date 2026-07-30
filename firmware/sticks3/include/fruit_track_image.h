#pragma once

#include <stdint.h>

#define FRUIT_TRACK_IMAGE_COUNT 24
#define FRUIT_TRACK_IMAGE_CELL 19

extern const uint16_t fruit_track_image_rgb565[
    FRUIT_TRACK_IMAGE_COUNT * FRUIT_TRACK_IMAGE_CELL * FRUIT_TRACK_IMAGE_CELL];
