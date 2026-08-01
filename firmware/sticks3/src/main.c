#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button_gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "fiery_dragon_image.h"
#include "fruit_audio.h"
#include "fruit_game_config.h"
#include "fruit_header_image.h"
#include "fruit_controls_image.h"
#include "fruit_controls_pending_image.h"
#include "fruit_track_image.h"
#include "vibe_board.h"

#define LCD_HOST SPI2_HOST
#define PANEL_H_RES 135
#define PANEL_V_RES 240
#define SCREEN_W 135
#define SCREEN_H 240
#define BOARD_X 1
#define BOARD_Y 22
#define BOARD_CELL 19
#define BOARD_SIZE (BOARD_CELL * 7)
#define LCD_X_GAP 52
#define LCD_Y_GAP 40
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_BACKLIGHT_PWM_HZ 5000
#define LCD_BACKLIGHT_DEFAULT 180
#define LVGL_DRAW_BUF_LINES 20
#define LVGL_TICK_PERIOD_MS 10
#define GAME_TIMER_MS 20
#define BATTERY_POLL_MS 5000

#define PIN_BUTTON_FRONT 11
#define PIN_BUTTON_SIDE 12
#define PIN_LCD_MOSI 39
#define PIN_LCD_SCK 40
#define PIN_LCD_DC 45
#define PIN_LCD_CS 41
#define PIN_LCD_RST 21
#define PIN_LCD_BL 38

#define CONTROL_COUNT 14
#define CONTROL_ALL 8
#define CONTROL_LEFT 9
#define CONTROL_RIGHT 10
#define CONTROL_SMALL 11
#define CONTROL_BIG 12
#define CONTROL_GO 13

typedef enum {
    EVENT_ACTIVATE,
    EVENT_ACTIVATE_LONG,
    EVENT_NEXT,
    EVENT_PREVIOUS,
    EVENT_GO,
    EVENT_ADD_CREDIT,
    EVENT_RESET_CREDIT,
    EVENT_TOGGLE_GEMS,
    EVENT_MOTION_LEFT,
    EVENT_MOTION_RIGHT,
    EVENT_MOTION_UP,
    EVENT_MOTION_DOWN,
} game_event_t;

typedef enum {
    STATE_IDLE,
    STATE_SPIN,
    STATE_BONUS_CHAIN,
    STATE_PENDING_WIN,
    STATE_BIG_BANG,
} game_state_t;

typedef enum {
    MSG_READY,
    MSG_NO_BET,
    MSG_NO_CREDIT,
    MSG_SPIN,
    MSG_MISS,
    MSG_WIN,
    MSG_LUCK_LEFT,
    MSG_LUCK_RIGHT,
    MSG_GAMBLE,
    MSG_GAMBLE_WIN,
    MSG_GAMBLE_LOSE,
    MSG_BET_DECREASED,
    MSG_CLEARED,
    MSG_BIG_BANG,
    MSG_FREE_SPIN,
    MSG_CREDIT_ADDED,
    MSG_CREDIT_RESET,
} message_t;

typedef struct {
    int32_t credit;
    int32_t high_credit;
    uint32_t total_rounds;
    uint32_t winning_rounds;
    int32_t highest_single_win;
    uint32_t settings_flags;
    uint8_t free_spins;
    uint8_t big_bang_armed;
    uint8_t balance_epoch;
    uint8_t gem_level;
    uint8_t bets[FRUIT_CATEGORY_COUNT];
    uint8_t prepaid_bets[FRUIT_CATEGORY_COUNT];
} saved_stats_t;

#define SETTING_SOUND_ENABLED (1U << 0)

static const char *TAG = "fruit_machine";
static QueueHandle_t s_events;
static SemaphoreHandle_t s_lvgl_lock;
static lv_display_t *s_display;
static esp_lcd_panel_handle_t s_panel;
static lv_obj_t *s_canvas;
static uint8_t *s_canvas_buffer;

static saved_stats_t s_stats = {
    .credit = FRUIT_STARTING_CREDIT,
    .high_credit = FRUIT_STARTING_CREDIT,
    .settings_flags = SETTING_SOUND_ENABLED,
    .big_bang_armed = 1,
    .gem_level = 1,
};
static uint8_t s_bets[FRUIT_CATEGORY_COUNT];
static uint8_t s_prepaid_bets[FRUIT_CATEGORY_COUNT];
static uint8_t s_selected_control = CONTROL_GO;
static uint8_t s_highlight;
static game_state_t s_state = STATE_IDLE;
static message_t s_message = MSG_READY;
static int s_pending_win;
static int s_last_award;
static int s_last_stake;
static int s_result_number;
static int s_gamble_amount;
static uint8_t s_gamble_level = FRUIT_GAMBLE_LEVEL_COUNT - 1;
static bool s_guess_big;
static int s_spin_steps;
static int s_spin_total;
static int s_spin_target;
static int64_t s_next_step_ms;
static int s_bonus_steps;
static int s_bonus_award;
static int s_bonus_step_award;
static int s_bonus_peak_multiplier;
static bool s_round_settled;
static uint8_t s_free_spins;
static bool s_current_spin_free;
static uint8_t s_lucky_loss_count;
static bool s_lucky_spin_active;
static int s_last_net;
static int64_t s_auto_spin_ms;
static bool s_big_bang_armed = true;
static uint8_t s_flash_steps;
static uint8_t s_fire_phase;
static uint8_t s_led_phase;
static bool s_gem_gallery;
static bool s_flash_red;
static volatile bool s_stats_dirty;
static int64_t s_stats_flush_ms;
static int s_battery_level=-1;
static bool s_battery_charging;
static bool s_usb_powered;
static int weighted_target(void);
static int weighted_fruit_target(void);
static int weighted_profitable_target(int stake);

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void play_sound(fruit_sound_t sound)
{
    if (s_stats.settings_flags & SETTING_SOUND_ENABLED) {
        fruit_audio_play(sound);
    }
}

static bool lvgl_lock(void)
{
    return s_lvgl_lock &&
        xSemaphoreTake(s_lvgl_lock, pdMS_TO_TICKS(250)) == pdTRUE;
}

static void lvgl_unlock(void)
{
    xSemaphoreGive(s_lvgl_lock);
}

static uint16_t color565(uint32_t rgb)
{
    return lv_color_to_u16(lv_color_hex(rgb));
}

static void pixel(int x, int y, uint32_t color)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        ((uint16_t *)s_canvas_buffer)[y * SCREEN_W + x] = color565(color);
    }
}

static void fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) { w = SCREEN_W - x; }
    if (y + h > SCREEN_H) { h = SCREEN_H - y; }
    if (w <= 0 || h <= 0) { return; }
    uint16_t value = color565(color);
    uint16_t *pixels = (uint16_t *)s_canvas_buffer;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            pixels[(y + row) * SCREEN_W + x + col] = value;
        }
    }
}

static void rect(int x, int y, int w, int h, uint32_t color)
{
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

static void fill_circle(int cx, int cy, int radius, uint32_t color)
{
    int rr = radius * radius;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= rr) { pixel(cx + x, cy + y, color); }
        }
    }
}

static const uint8_t s_font[36][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
};

static const uint8_t *glyph(char c)
{
    if (c >= 'A' && c <= 'Z') { return s_font[c - 'A']; }
    if (c >= '0' && c <= '9') { return s_font[26 + c - '0']; }
    return NULL;
}

static void text(const char *value, int x, int y, int scale, uint32_t color)
{
    for (const char *p = value; *p; ++p) {
        if (*p == ' ') { x += 4 * scale; continue; }
        if (*p == '+' || *p == '-') {
            if (*p == '+') {
                fill_rect(x + 2 * scale, y + scale, scale, 5 * scale, color);
            }
            fill_rect(x, y + 3 * scale, 5 * scale, scale, color);
            x += 6 * scale;
            continue;
        }
        const uint8_t *rows = glyph(*p);
        if (!rows) { x += 6 * scale; continue; }
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (rows[row] & (1 << (4 - col))) {
                    fill_rect(x + col * scale, y + row * scale,
                              scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static int text_width(const char *value, int scale)
{
    int width = 0;
    for (const char *p = value; *p; ++p) {
        width += *p == ' ' ? 4 * scale : 6 * scale;
    }
    return width ? width - scale : 0;
}

static void centered_text(const char *value, int cx, int y, int scale,
                          uint32_t color)
{
    text(value, cx - text_width(value, scale) / 2, y, scale, color);
}

static void track_position(int index, int *x, int *y, int *w, int *h)
{
    *w = BOARD_CELL;
    *h = BOARD_CELL;
    if (index < 7) {
        *x = BOARD_X + index * BOARD_CELL;
        *y = BOARD_Y;
    } else if (index < 12) {
        *x = BOARD_X + 6 * BOARD_CELL;
        *y = BOARD_Y + (index - 6) * BOARD_CELL;
    } else if (index < 19) {
        *x = BOARD_X + (18 - index) * BOARD_CELL;
        *y = BOARD_Y + 6 * BOARD_CELL;
    } else {
        *x = BOARD_X;
        *y = BOARD_Y + (24 - index) * BOARD_CELL;
    }
}

static int total_bet(void)
{
    int total = 0;
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        total+=s_bets[i]*fruit_bet_costs[i];
    }
    return total;
}

static int prepaid_bet_cost(void)
{
    int total=0;
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        total+=s_prepaid_bets[i]*fruit_bet_costs[i];
    }
    return total;
}

static int cell_award(int index)
{
    const fruit_track_cell_t *cell = &fruit_track[index];
    if (cell->symbol==FRUIT_SYMBOL_LUCK) return 0;
    int category = cell->symbol;
    int payout = cell->payout_override
        ? cell->payout_override : fruit_base_payouts[category];
    return s_bets[category]*payout*cell->multiplier;
}

static int cell_effective_multiplier(int index)
{
    const fruit_track_cell_t *cell=&fruit_track[index];
    if (cell->luck!=FRUIT_LUCK_NONE) return 0;
    if (cell->symbol>=FRUIT_CATEGORY_COUNT) return 0;
    int payout=cell->payout_override
        ?cell->payout_override:fruit_base_payouts[cell->symbol];
    return payout*cell->multiplier;
}

static int bonus_cell_award(int index)
{
    /* A blue-LUCK chain settles fruit cells only and never chains LUCK. */
    if (fruit_track[index].symbol==FRUIT_SYMBOL_LUCK) return 0;
    return cell_award(index);
}

static void blit_track_tile(int tile_index, int x, int y)
{
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;
    const uint16_t *tile=&fruit_track_image_rgb565[
        tile_index*FRUIT_TRACK_IMAGE_CELL*FRUIT_TRACK_IMAGE_CELL];
    for (int row=0;row<FRUIT_TRACK_IMAGE_CELL;++row) {
        memcpy(&pixels[(y+row)*SCREEN_W+x],
               &tile[row*FRUIT_TRACK_IMAGE_CELL],
               FRUIT_TRACK_IMAGE_CELL*sizeof(uint16_t));
    }
}

static void blit_bet_tile_without_top_divider(int tile_index, int x, int y)
{
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;
    const uint16_t *tile=&fruit_track_image_rgb565[
        tile_index*FRUIT_TRACK_IMAGE_CELL*FRUIT_TRACK_IMAGE_CELL];
    const int cropped_height=FRUIT_TRACK_IMAGE_CELL-3;
    for (int row=0;row<FRUIT_TRACK_IMAGE_CELL;++row) {
        int source_row=3+row*cropped_height/FRUIT_TRACK_IMAGE_CELL;
        memcpy(&pixels[(y+row)*SCREEN_W+x],
               &tile[source_row*FRUIT_TRACK_IMAGE_CELL],
               FRUIT_TRACK_IMAGE_CELL*sizeof(uint16_t));
    }
}

static void blit_bet_tile_inset(int tile_index, int x, int y)
{
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;
    const uint16_t *tile=&fruit_track_image_rgb565[
        tile_index*FRUIT_TRACK_IMAGE_CELL*FRUIT_TRACK_IMAGE_CELL];
    const int inset_size=FRUIT_TRACK_IMAGE_CELL-2;
    for (int row=0;row<inset_size;++row) {
        int source_row=row*FRUIT_TRACK_IMAGE_CELL/inset_size;
        for (int column=0;column<inset_size;++column) {
            int source_column=column*FRUIT_TRACK_IMAGE_CELL/inset_size;
            pixels[(y+row)*SCREEN_W+x+column]=
                tile[source_row*FRUIT_TRACK_IMAGE_CELL+source_column];
        }
    }
}

static void light_track_tile(int x, int y, int w, int h)
{
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;
    bool flare=(s_led_phase&1)!=0;
    for (int row=1;row<h-1;++row) {
        for (int column=1;column<w-1;++column) {
            uint16_t source=pixels[(y+row)*SCREEN_W+x+column];
            int red=(source>>11)&0x1f;
            int green=(source>>5)&0x3f;
            int blue=source&0x1f;
            int dx=column-w/2;
            int dy=row-h/2;
            int radial=dx*dx+dy*dy;
            int strength=(radial<36)?(flare?3:2):1;

            /* Warm backlight: retain the artwork while lifting it toward
             * incandescent yellow as the moving frame reaches this cell. */
            red+=(31-red)*strength/4;
            green+=(63-green)*strength/5;
            blue+=(10-blue)*strength/6;
            if (red>31) red=31;
            if (green>63) green=63;
            if (blue<0) blue=0;
            if (blue>31) blue=31;
            pixels[(y+row)*SCREEN_W+x+column]=
                (uint16_t)((red<<11)|(green<<5)|blue);
        }
    }
}

static void draw_track_x3(int center_x, int y)
{
    static const uint8_t x_rows[5]={5,5,2,5,5};
    static const uint8_t three_rows[5]={7,1,7,1,7};
    const int start_x=center_x-3;
    for (int row=0;row<5;++row) {
        for (int column=0;column<3;++column) {
            uint8_t mask=1<<(2-column);
            if (x_rows[row]&mask) {
                fill_rect(start_x+column,y+row,1,1,0x1c1008);
            }
            if (three_rows[row]&mask) {
                fill_rect(start_x+4+column,y+row,1,1,0x1c1008);
            }
        }
    }
}

static void draw_track(void)
{
    bool animated=s_state==STATE_SPIN || s_state==STATE_BONUS_CHAIN;
    for (int i = 0; i < FRUIT_TRACK_COUNT; ++i) {
        int x,y,w,h; track_position(i,&x,&y,&w,&h);
        blit_track_tile(i,x,y);
        if (animated && i==s_highlight) {
            light_track_tile(x,y,w,h);
        }
        if (fruit_track[i].symbol==FRUIT_SYMBOL_BAR) {
            fill_rect(x+1,y+2,w-2,7,0x65166f);
            centered_text("SUP",x+w/2,y+2,1,0xffffff);
            fill_rect(x+1,y+10,w-2,7,0xffe5af);
            char payout[8];
            snprintf(payout,sizeof(payout),"%d",fruit_track[i].payout_override);
            centered_text(payout,x+w/2,y+10,1,0x1c1008);
        } else if (fruit_track[i].multiplier==3) {
            /* Redraw tiny source lettering after 19x19 downsampling. */
            fill_rect(x+5,y+12,w-10,5,0xffe5af);
            draw_track_x3(x+w/2,y+12);
        }
        rect(x,y,w,h,0x6d2b0b);
    }

    int x,y,w,h;
    track_position(s_highlight,&x,&y,&w,&h);
    if (animated) {
        bool flare=(s_led_phase&1)!=0;
        rect(x-1,y-1,w+2,h+2,flare?0xffc400:0x9b3d00);
        rect(x,y,w,h,flare?0xffffff:0xffe22f);
        rect(x+1,y+1,w-2,h-2,flare?0xff8a00:0xffb000);
        fill_circle(x+2,y+2,1,flare?0xffffff:0xffe36a);
        fill_circle(x+w-3,y+2,1,flare?0xffffff:0xffe36a);
        fill_circle(x+2,y+h-3,1,flare?0xffffff:0xffe36a);
        fill_circle(x+w-3,y+h-3,1,flare?0xffffff:0xffe36a);
    } else {
        rect(x,y,w,h,0xffee36);
        rect(x+1,y+1,w-2,h-2,0xff9d00);
    }
}

static void inner_led_position(int index, int *led_x, int *led_y)
{
    int tile_x,tile_y,tile_w,tile_h;
    track_position(index,&tile_x,&tile_y,&tile_w,&tile_h);
    const int left=BOARD_X+BOARD_CELL+2;
    const int top=BOARD_Y+BOARD_CELL+2;
    const int right=BOARD_X+BOARD_CELL*6-3;
    const int bottom=BOARD_Y+BOARD_CELL*6-3;
    if (index<=6) {
        *led_x=tile_x+tile_w/2;
        if (*led_x<left) *led_x=left;
        if (*led_x>right) *led_x=right;
        *led_y=top;
    } else if (index<=11) {
        *led_x=right;
        *led_y=tile_y+tile_h/2;
    } else if (index<=18) {
        *led_x=tile_x+tile_w/2;
        if (*led_x<left) *led_x=left;
        if (*led_x>right) *led_x=right;
        *led_y=bottom;
    } else {
        *led_x=left;
        *led_y=tile_y+tile_h/2;
    }
}

static void draw_inner_led_ring(void)
{
    bool animated=s_state==STATE_SPIN || s_state==STATE_BONUS_CHAIN;
    if (!animated) return;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        int led_x,led_y;
        inner_led_position(i,&led_x,&led_y);
        int distance=(s_highlight-i+FRUIT_TRACK_COUNT)%FRUIT_TRACK_COUNT;
        if (distance==0) {
            fill_circle(led_x,led_y,3,0xff5a00);
            fill_circle(led_x,led_y,2,
                        (s_led_phase&1)?0xffff50:0xffb000);
            fill_circle(led_x,led_y,1,
                        (s_led_phase&1)?0xffffff:0xffff9a);
        } else if (distance==1) {
            fill_circle(led_x,led_y,2,0xff7a00);
            fill_circle(led_x,led_y,1,0xffd22f);
        } else if (distance==2) {
            fill_circle(led_x,led_y,1,0xff7a00);
        }
    }
}

static void draw_lucky_meter(void)
{
    /* Four small lamps centered on the inner rail, clear of the track tiles. */
    const int center_x=BOARD_X+BOARD_SIZE/2;
    const int y=BOARD_Y+BOARD_CELL+4;
    for (int lamp=0;lamp<4;++lamp) {
        int x=center_x-15+lamp*10;
        bool lit=lamp<s_lucky_loss_count;
        bool flash=s_lucky_spin_active && (s_led_phase&1)==0;
        fill_circle(x,y,2,0x32100a);
        fill_circle(x,y,1,lit?(flash?0xffffff:0xffbd20):0x64180e);
        if (lit) pixel(x,y,flash?0xffff8a:0xffff3a);
    }
}

static void draw_mini_seven_segment_digit(int digit, int x, int y,
                                          uint32_t color)
{
    static const uint8_t map[10] = {
        0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f
    };
    uint8_t m = map[digit % 10];
    if (m&1) fill_rect(x+1,y,5,1,color);
    if (m&2) fill_rect(x+6,y+1,1,4,color);
    if (m&4) fill_rect(x+6,y+6,1,4,color);
    if (m&8) fill_rect(x+1,y+10,5,1,color);
    if (m&16) fill_rect(x,y+6,1,4,color);
    if (m&32) fill_rect(x,y+1,1,4,color);
    if (m&64) fill_rect(x+1,y+5,5,1,color);
}

static void draw_header_seven_segment_digit(int digit, int x, int y,
                                            uint32_t color)
{
    static const uint8_t map[10] = {
        0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f
    };
    uint8_t m=map[digit%10];
    if (m&1) fill_rect(x+1,y,3,1,color);
    if (m&2) fill_rect(x+4,y+1,1,2,color);
    if (m&4) fill_rect(x+4,y+4,1,2,color);
    if (m&8) fill_rect(x+1,y+6,3,1,color);
    if (m&16) fill_rect(x,y+4,1,2,color);
    if (m&32) fill_rect(x,y+1,1,2,color);
    if (m&64) fill_rect(x+1,y+3,3,1,color);
}

static void draw_bet_digit(int digit, int x, int y, uint32_t color)
{
    static const uint8_t rows[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
        {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2},
        {7,5,7,5,7}, {7,5,7,1,7},
    };
    for (int row=0;row<5;++row) {
        for (int column=0;column<3;++column) {
            if (rows[digit%10][row]&(1<<(2-column))) {
                fill_rect(x+column,y+row,1,1,color);
            }
        }
    }
}

static void draw_bet_number(int value, int center_x, int y, uint32_t color)
{
    int start_x=center_x-3;
    draw_bet_digit((value/10)%10,start_x,y,color);
    draw_bet_digit(value%10,start_x+4,y,color);
}

static void draw_header_number(int value, int x, int slots, int minimum_digits)
{
    char number[12];
    bool negative=value<0;
    uint32_t magnitude=negative
        ?(uint32_t)(-(int64_t)value):(uint32_t)value;
    snprintf(number,sizeof(number),"%lu",(unsigned long)magnitude);
    int length=(int)strlen(number);
    if (length<minimum_digits) length=minimum_digits;
    int digit_slots=negative?slots-1:slots;
    if (length>digit_slots) length=digit_slots;
    for (int i=0;i<slots;++i) {
        draw_header_seven_segment_digit(8,x+i*6,10,0x32120f);
    }
    int divisor=1;
    for (int i=1;i<length;++i) divisor*=10;
    int start=slots-length;
    if (negative) {
        fill_rect(x+(start-1)*6,13,5,1,0xff6848);
    }
    for (int i=0;i<length;++i) {
        int digit=(magnitude/divisor)%10;
        draw_header_seven_segment_digit(digit,x+(start+i)*6,10,0xff6848);
        if (divisor>1) divisor/=10;
    }
}

static const uint8_t *header_label_glyph(char c)
{
    static const uint8_t glyphs[][5] = {
        {6,5,6,5,6}, /* B */
        {3,4,4,4,3}, /* C */
        {6,5,5,5,6}, /* D */
        {7,4,6,4,7}, /* E */
        {5,5,7,5,5}, /* H */
        {7,2,2,2,7}, /* I */
        {5,7,7,5,5}, /* M */
        {5,7,7,7,5}, /* N */
        {2,5,5,5,2}, /* O */
        {6,5,6,5,5}, /* R */
        {3,4,2,1,6}, /* S */
        {7,2,2,2,2}, /* T */
        {5,5,5,5,7}, /* U */
        {5,5,7,7,5}, /* W */
    };
    static const char letters[]="BCDEHIMNORSTUW";
    const char *found=strchr(letters,c);
    return found?glyphs[found-letters]:NULL;
}

static void draw_header_label(const char *value, int x, int y)
{
    for (const char *p=value;*p;++p) {
        if (*p==' ') {
            x+=2;
            continue;
        }
        const uint8_t *rows=header_label_glyph(*p);
        if (rows) {
            for (int row=0;row<5;++row) {
                for (int column=0;column<3;++column) {
                    if (rows[row]&(1<<(2-column))) {
                        pixel(x+column,y+row,0xffd28a);
                    }
                }
            }
        }
        x+=4;
    }
}

static void draw_battery_status(void)
{
    const int body_x=112;
    const int body_y=1;
    const int body_w=21;
    const int body_h=4;
    const int inner_w=body_w-2;
    const uint32_t color=0xff6848;

    fill_rect(body_x,0,23,6,0x090705);
    fill_rect(body_x+1,body_y,body_w-2,1,color);
    fill_rect(body_x+1,body_y+body_h-1,body_w-2,1,color);
    fill_rect(body_x,body_y+1,1,body_h-2,color);
    if (s_battery_level<0) {
        pixel(body_x+8,body_y+1,color);
        pixel(body_x+11,body_y+2,color);
        return;
    }
    int fill_w=(s_battery_level*inner_w+99)/100;
    if (fill_w>inner_w) fill_w=inner_w;
    if (fill_w>0) fill_rect(body_x+1,body_y+1,fill_w,body_h-2,color);
    if (s_battery_charging) {
        const int bolt_x=body_x+body_w/2;
        fill_rect(bolt_x-3,body_y+1,7,body_h-2,0x090705);
        pixel(bolt_x+3,body_y+1,color);
        pixel(bolt_x+2,body_y+1,color);
        pixel(bolt_x+1,body_y+1,color);
        pixel(bolt_x,body_y+1,color);
        pixel(bolt_x-3,body_y+2,color);
        pixel(bolt_x-2,body_y+2,color);
        pixel(bolt_x-1,body_y+2,color);
        pixel(bolt_x,body_y+2,color);
    }
}

static const uint16_t s_gem_masks[6][9] = {
    {0x010,0x038,0x07c,0x0fe,0x1ff,0x0fe,0x07c,0x038,0x010},
    {0x038,0x07c,0x0fe,0x1ff,0x1ff,0x1ff,0x0fe,0x07c,0x038},
    {0x0fe,0x1ff,0x1ff,0x1ff,0x1ff,0x1ff,0x1ff,0x1ff,0x0fe},
    {0x010,0x038,0x07c,0x0fe,0x1ff,0x1ff,0x0fe,0x07c,0x038},
    {0x07c,0x07c,0x0fe,0x0fe,0x1ff,0x0fe,0x0fe,0x07c,0x07c},
    {0x010,0x054,0x0fe,0x07c,0x1ff,0x07c,0x0fe,0x054,0x010},
};

static const uint32_t s_gem_colors[30] = {
    0xc77a20,0xd8758b,0x38a85b,0x36a8c6,0x8a55c7,0xd4ad26,
    0xa92836,0x86b93f,0x9db2bf,0x2456c7,0xd52f43,0x15966a,
    0xd88b25,0xd7c9ec,0x5451c8,0xd34f8f,0xdc394d,0x5bc7d6,
    0xec3655,0xb39b3f,0x4d5667,0xf16faf,0x388ee9,0x43d17d,
    0xf36b28,0x74d8c7,0xffd24a,0xa9b8ff,0xef3b2d,0xf4e06d,
};

static const char *const s_gem_names[30] = {
    "Amber Chip","Rose Quartz","Jade Drop","Aquamarine","Amethyst",
    "Citrine","Garnet","Peridot","Moonstone","Sapphire","Ruby",
    "Emerald","Topaz","Opal","Tanzanite","Tourmaline","Spinel",
    "Zircon","Star Ruby","Cat Eye","Black Diamond","Pink Diamond",
    "Blue Diamond","Green Diamond","Fire Opal","Aurora Crystal",
    "Solar Prism","Lunar Prism","Dragon Heart","Crown Jewel",
};

static uint32_t gem_unlock_threshold(uint8_t level)
{
    if (level<=1) return 0;
    return 50U*(level-1U)*level;
}

static uint8_t gem_level_from_credit(int32_t credit)
{
    uint32_t value=credit>0?(uint32_t)credit:0;
    uint8_t level=1;
    while (level<30 && value>=gem_unlock_threshold(level+1)) {
        ++level;
    }
    return level;
}

static uint8_t gem_level_after_credit(uint8_t activated_level, int32_t credit)
{
    uint8_t reached_level=gem_level_from_credit(credit);
    return reached_level>activated_level?reached_level:activated_level;
}

static uint32_t gem_brighten(uint32_t color)
{
    int red=(color>>16)&0xff;
    int green=(color>>8)&0xff;
    int blue=color&0xff;
    red+=(255-red)*2/3;
    green+=(255-green)*2/3;
    blue+=(255-blue)*2/3;
    return (uint32_t)((red<<16)|(green<<8)|blue);
}

static uint32_t gem_shadow(uint32_t color)
{
    return (uint32_t)((((color>>16)&0xff)/3<<16) |
                      (((color>>8)&0xff)/3<<8) |
                      ((color&0xff)/3));
}

static bool gem_mask_contains(const uint16_t *mask, int row, int column)
{
    return row>=0 && row<9 && column>=0 && column<9 &&
        (mask[row]&(1<<(8-column)))!=0;
}

static void draw_header_gem(void)
{
    uint8_t level=s_stats.gem_level;
    const uint16_t *mask=s_gem_masks[(level-1)%6];
    uint32_t raw_color=s_gem_colors[level-1];
    bool animated=s_state==STATE_SPIN || s_state==STATE_BONUS_CHAIN;
    bool flare=animated && (s_led_phase&1)==0;
    uint32_t base=flare?gem_brighten(raw_color):raw_color;
    uint32_t highlight=gem_brighten(base);
    uint32_t shadow=flare?raw_color:gem_shadow(base);
    const int start_x=63;
    const int start_y=8;

    for (int row=0;row<9;++row) {
        for (int column=0;column<9;++column) {
            if (!gem_mask_contains(mask,row,column)) continue;
            bool edge=!gem_mask_contains(mask,row-1,column) ||
                !gem_mask_contains(mask,row+1,column) ||
                !gem_mask_contains(mask,row,column-1) ||
                !gem_mask_contains(mask,row,column+1);
            uint32_t color=flare?gem_brighten(shadow):0x2a1208;
            if (!edge) {
                if (row<=3 && column<=4) color=highlight;
                else if (row>=5 || column>=6) color=shadow;
                else color=base;
            }
            pixel(start_x+column,start_y+row,color);
        }
    }

    int tier=(level-1)/6;
    if (tier>=1 && gem_mask_contains(mask,2,3)) pixel(66,10,highlight);
    if (tier>=2 && gem_mask_contains(mask,5,5)) pixel(68,13,highlight);
    if (tier>=3 && gem_mask_contains(mask,5,2)) pixel(65,13,0xffffff);
    if (tier>=4 && gem_mask_contains(mask,2,6)) pixel(69,10,0xffffff);
    pixel(67,12,flare?0xffffff:highlight);
}

static void draw_gallery_gem(uint8_t level, int start_x, int start_y,
                             bool unlocked)
{
    const uint16_t *mask=s_gem_masks[(level-1)%6];
    uint32_t base=unlocked?s_gem_colors[level-1]:0x4d4d4d;
    uint32_t highlight=unlocked?gem_brighten(base):0x777777;
    uint32_t shadow=unlocked?gem_shadow(base):0x242424;
    const int scale=2;
    for (int row=0;row<9;++row) {
        for (int column=0;column<9;++column) {
            if (!gem_mask_contains(mask,row,column)) continue;
            bool edge=!gem_mask_contains(mask,row-1,column) ||
                !gem_mask_contains(mask,row+1,column) ||
                !gem_mask_contains(mask,row,column-1) ||
                !gem_mask_contains(mask,row,column+1);
            uint32_t color=0x160c08;
            if (!edge) {
                if (row<=3 && column<=4) color=highlight;
                else if (row>=5 || column>=6) color=shadow;
                else color=base;
            }
            fill_rect(start_x+column*scale,start_y+row*scale,
                      scale,scale,color);
        }
    }
    int tier=(level-1)/6;
    if (tier>=1 && gem_mask_contains(mask,2,3)) {
        fill_rect(start_x+3*scale,start_y+2*scale,scale,scale,highlight);
    }
    if (tier>=2 && gem_mask_contains(mask,5,5)) {
        fill_rect(start_x+5*scale,start_y+5*scale,scale,scale,highlight);
    }
    if (tier>=3 && gem_mask_contains(mask,5,2)) {
        fill_rect(start_x+2*scale,start_y+5*scale,scale,scale,
                  unlocked?0xffffff:0x8a8a8a);
    }
    if (tier>=4 && gem_mask_contains(mask,2,6)) {
        fill_rect(start_x+6*scale,start_y+2*scale,scale,scale,
                  unlocked?0xffffff:0x8a8a8a);
    }
}

static void draw_gallery_number(uint32_t value, int center_x, int y,
                                uint32_t color)
{
    char number[8];
    snprintf(number,sizeof(number),"%lu",(unsigned long)value);
    int length=(int)strlen(number);
    int width=length*4-1;
    int x=center_x-width/2;
    for (int i=0;i<length;++i) {
        draw_bet_digit(number[i]-'0',x+i*4,y,color);
    }
}

static void draw_gem_gallery(void)
{
    fill_rect(0,0,SCREEN_W,SCREEN_H,0x090705);
    uint8_t unlocked_level=s_stats.gem_level;
    for (int index=0;index<30;++index) {
        int column=index%5;
        int row=index/5;
        int x=column*27;
        int y=row*40;
        bool unlocked=index<unlocked_level;
        fill_rect(x,y,27,40,((row+column)&1)?0x120d0b:0x18110d);
        rect(x,y,27,40,0x3b281d);
        draw_gallery_gem((uint8_t)(index+1),x+4,y+4,unlocked);
        draw_gallery_number(gem_unlock_threshold((uint8_t)(index+1)),
                            x+13,y+28,
                            unlocked?0xffd56a:0x646464);
    }
}

static void draw_castle_and_dragon(void)
{
    const int ix = BOARD_X + BOARD_CELL;
    const int iy = BOARD_Y + BOARD_CELL;
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;
    for (int row=0;row<FIERY_DRAGON_IMAGE_HEIGHT;++row) {
        memcpy(&pixels[(iy+row)*SCREEN_W+ix],
               &fiery_dragon_image_rgb565[row*FIERY_DRAGON_IMAGE_WIDTH],
               FIERY_DRAGON_IMAGE_WIDTH*sizeof(uint16_t));
    }
    if (s_state==STATE_BIG_BANG) {
        int reach=10+(s_fire_phase%4)*4;
        for (int i=0;i<reach;++i) {
            int fy=iy+65+((i+s_fire_phase)&3)-2;
            pixel(ix+73-i,fy,0xfff15a);
            pixel(ix+73-i,fy+1,i<reach/2?0xff8a24:0xff3528);
        }
        fill_circle(ix+71,iy+65,2+(s_fire_phase&1),0xff5722);
    }
}

static void pulse_center_cn(void)
{
    if (s_state!=STATE_SPIN && s_state!=STATE_BONUS_CHAIN) return;
    const int ix=BOARD_X+BOARD_CELL;
    const int iy=BOARD_Y+BOARD_CELL;
    bool flare=(s_led_phase&1)==0;
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;

    /* Restrict the pulse to the yellow C/N letter pixels inside the sign. */
    for (int y=50;y<59;++y) {
        for (int x=43;x<58;++x) {
            uint16_t source=pixels[(iy+y)*SCREEN_W+ix+x];
            int red=(source>>11)&0x1f;
            int green=(source>>5)&0x3f;
            int blue=source&0x1f;
            int red8=red*255/31;
            int green8=green*255/63;
            int blue8=blue*255/31;
            if (red8<90 || green8<70 || blue8>60 ||
                green8*100<red8*45) {
                continue;
            }
            if (flare) {
                red+=(31-red)*3/4;
                green+=(63-green)*3/4;
                blue/=2;
            } else {
                red=red*3/4;
                green=green*2/3;
                blue/=2;
            }
            pixels[(iy+y)*SCREEN_W+ix+x]=
                (uint16_t)((red<<11)|(green<<5)|blue);
        }
    }
}

static const char *message_text(char *buffer, size_t size)
{
    switch (s_message) {
    case MSG_NO_BET: return "ADD BET";
    case MSG_NO_CREDIT: return "NO CREDIT";
    case MSG_SPIN: return "GOOD LUCK";
    case MSG_MISS: return "TRY AGAIN";
    case MSG_WIN: snprintf(buffer,size,"WIN %d",s_last_award); return buffer;
    case MSG_LUCK_LEFT: snprintf(buffer,size,"LUCK %d",s_pending_win); return buffer;
    case MSG_LUCK_RIGHT: snprintf(buffer,size,"CHAIN %d",s_bonus_award); return buffer;
    case MSG_GAMBLE: return "PENDING WIN";
    case MSG_GAMBLE_WIN: snprintf(buffer,size,"N%02d X2",s_result_number); return buffer;
    case MSG_GAMBLE_LOSE: snprintf(buffer,size,"N%02d LOST",s_result_number); return buffer;
    case MSG_BET_DECREASED: return "BET -1";
    case MSG_CLEARED: return "BET CLEARED";
    case MSG_BIG_BANG: return "BIG BANG";
    case MSG_FREE_SPIN: snprintf(buffer,size,"FREE %d",s_free_spins); return buffer;
    case MSG_CREDIT_ADDED:
        snprintf(buffer,size,"CREDIT +%d",FRUIT_CREDIT_TOPUP_UNIT);
        return buffer;
    case MSG_CREDIT_RESET: return "CREDIT RESET";
    default: return "SELECT AND PLAY";
    }
}

static void draw_center(void)
{
    draw_castle_and_dragon();
    pulse_center_cn();
    const int ix = BOARD_X + BOARD_CELL;
    const int iy = BOARD_Y + BOARD_CELL;
    rect(ix,iy,BOARD_CELL*5,BOARD_CELL*5,0xb45924);
    fill_rect(ix+36,iy+76,23,14,0x100807);
    rect(ix+35,iy+75,25,16,0x8d3226);
    int shown = s_result_number;
    if (s_state == STATE_SPIN || s_state == STATE_BONUS_CHAIN) {
        shown = s_highlight;
    }
    draw_mini_seven_segment_digit((shown/10)%10,ix+39,iy+77,0xff6a42);
    draw_mini_seven_segment_digit(shown%10,ix+49,iy+77,0xff6a42);
}

static void draw_feedback_message(void)
{
    const int cx=BOARD_X+BOARD_CELL*7/2;
    if (s_state==STATE_PENDING_WIN) {
        char win[24];
        char risk[24];
        char safe[24];
        int percentage=fruit_gamble_percentages[s_gamble_level];
        int safe_amount=s_pending_win-s_gamble_amount;
        snprintf(win,sizeof(win),"WIN %d",s_pending_win);
        snprintf(risk,sizeof(risk),"RISK %d%%",percentage);
        snprintf(safe,sizeof(safe),"SAFE %d",safe_amount);
        int width=text_width(win,1);
        int candidate=text_width(risk,1);
        if (candidate>width) width=candidate;
        candidate=text_width(safe,1);
        if (candidate>width) width=candidate;
        width+=8;
        if (width>89) width=89;
        const int y=BOARD_Y+BOARD_CELL+37;
        fill_rect(cx-width/2,y,width,31,0x160a08);
        rect(cx-width/2,y,width,31,0xff9d00);
        centered_text(win,cx,y+2,1,0xffe4a3);
        centered_text(risk,cx,y+12,1,0xffc43d);
        centered_text(safe,cx,y+22,1,0x9dff7a);
        return;
    }
    bool result_status=s_round_settled &&
        (s_message==MSG_MISS || s_message==MSG_WIN ||
         s_message==MSG_GAMBLE);
    if (result_status) {
        char bet[24];
        char win[24];
        char net[24];
        int paid_stake=s_current_spin_free?0:s_last_stake;
        snprintf(bet,sizeof(bet),"BET %d",paid_stake);
        snprintf(win,sizeof(win),"WIN %d",s_last_award);
        snprintf(net,sizeof(net),"NET %c%d",
                 s_last_net>=0?'+':'-',abs(s_last_net));
        int width=text_width(bet,1);
        int candidate=text_width(win,1);
        if (candidate>width) width=candidate;
        candidate=text_width(net,1);
        if (candidate>width) width=candidate;
        width+=8;
        if (width>89) width=89;
        const int y=BOARD_Y+BOARD_CELL+37;
        fill_rect(cx-width/2,y,width,31,0x160a08);
        rect(cx-width/2,y,width,31,0xff9d00);
        centered_text(bet,cx,y+2,1,0xffe4a3);
        centered_text(win,cx,y+12,1,0xffe4a3);
        centered_text(net,cx,y+22,1,
                      s_last_net>=0?0x9dff7a:0xff8a70);
        return;
    }
    if (s_state==STATE_BONUS_CHAIN) {
        char top[24];
        char bottom[24];
        snprintf(top,sizeof(top),"STEP +%d",s_bonus_step_award);
        snprintf(bottom,sizeof(bottom),"TOTAL %d",s_bonus_award);
        int width=text_width(top,1);
        int bottom_width=text_width(bottom,1);
        if (bottom_width>width) width=bottom_width;
        width+=8;
        if (width>89) width=89;
        const int y=BOARD_Y+BOARD_CELL+47;
        fill_rect(cx-width/2,y,width,21,0x160a08);
        rect(cx-width/2,y,width,21,0xff9d00);
        centered_text(top,cx,y+2,1,0xffe4a3);
        centered_text(bottom,cx,y+12,1,0x9dff7a);
        return;
    }
    if (s_message==MSG_READY) return;
    char buffer[24];
    const char *message=message_text(buffer,sizeof(buffer));
    const int y=BOARD_Y+BOARD_CELL+57;
    int width=text_width(message,1)+8;
    if (width>89) width=89;
    fill_rect(cx-width/2,y,width,11,0x160a08);
    rect(cx-width/2,y,width,11,0xff9d00);
    centered_text(message,cx,y+2,1,0xffe4a3);
}

static void draw_header(void)
{
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;
    for (int row=0;row<FRUIT_HEADER_IMAGE_HEIGHT;++row) {
        memcpy(&pixels[row*SCREEN_W],
               &fruit_header_image_rgb565[row*FRUIT_HEADER_IMAGE_WIDTH],
               FRUIT_HEADER_IMAGE_WIDTH*sizeof(uint16_t));
    }
    /* Replace the tiny centered source labels with crisp left-aligned labels.
     * The values occupy the right edge of each display window. */
    fill_rect(1,0,59,7,0x090705);
    fill_rect(76,0,59,7,0x090705);
    fill_rect(2,6,57,1,0xb45924);
    fill_rect(77,6,57,1,0xb45924);
    fill_rect(59,0,18,20,0x050405);
    fill_rect(59,6,1,14,0xb45924);
    fill_rect(76,6,1,14,0xb45924);
    draw_header_label("BONUS WIN",3,0);
    draw_header_label("CREDIT",78,0);
    draw_battery_status();
    draw_header_number(
        s_state==STATE_PENDING_WIN?s_gamble_amount:s_pending_win,23,6,3);
    draw_header_number(s_stats.credit,98,6,3);
    draw_header_gem();
    if (s_flash_red) {
        rect(0,0,SCREEN_W,BOARD_Y,0xff3028);
        rect(1,1,SCREEN_W-2,BOARD_Y-2,0x8f0909);
    }
}

static bool control_button_shape_contains(int index, int local_x, int local_y,
                                          int width)
{
    if (index==0 || index==3 || index==4) {
        int radius_x=index==0?9:8;
        int radius_y=12;
        int center_x=width/2+(index==0?1:-1);
        int dx=local_x-center_x;
        int dy=local_y-20;
        return dx*dx*radius_y*radius_y+
            dy*dy*radius_x*radius_x<=
            radius_x*radius_x*radius_y*radius_y;
    }
    if (index==1 || index==2) {
        const int left=1;
        const int right=width-2;
        const int top=9;
        const int bottom=33;
        const int radius=5;
        int nearest_x=local_x;
        int nearest_y=local_y;
        if (nearest_x<left+radius) nearest_x=left+radius;
        if (nearest_x>right-radius) nearest_x=right-radius;
        if (nearest_y<top+radius) nearest_y=top+radius;
        if (nearest_y>bottom-radius) nearest_y=bottom-radius;
        int dx=local_x-nearest_x;
        int dy=local_y-nearest_y;
        return local_x>=left && local_x<=right &&
            local_y>=top && local_y<=bottom &&
            dx*dx+dy*dy<=radius*radius;
    }
    if (index==5 && local_y>=9 && local_y<=30) {
        /*
         * The GO artwork is an asymmetric perspective trapezoid: its left
         * edge begins at the slot boundary while both edges drift right
         * toward the bottom.  Match that actual silhouette instead of using
         * a centered trapezoid.
         */
        int row=local_y-9;
        int left=(row*2+10)/21;
        int right=22+(row*3+10)/21;
        if (local_y==9 || local_y==30) {
            ++left;
            --right;
        }
        return local_x>=left && local_x<=right && local_x<width;
    }
    return false;
}

static void draw_control_selection_outline(int index, int x, int w)
{
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;
    for (int local_y=0;local_y<40;++local_y) {
        int y=155+local_y;
        for (int local_x=0;local_x<w;++local_x) {
            if (!control_button_shape_contains(
                    index,local_x,local_y,w)) continue;
            bool edge=false;
            for (int offset_y=-1;offset_y<=1 && !edge;++offset_y) {
                for (int offset_x=-1;offset_x<=1;++offset_x) {
                    if (offset_x==0 && offset_y==0) continue;
                    if (!control_button_shape_contains(
                            index,local_x+offset_x,local_y+offset_y,w)) {
                        edge=true;
                        break;
                    }
                }
            }
            if (edge) {
                pixels[y*SCREEN_W+x+local_x]=0xffe0;
            }
        }
    }
}

static void draw_controls(void)
{
    static const uint8_t widths[6] = {24,22,21,19,19,30};
    uint16_t *pixels=(uint16_t *)s_canvas_buffer;

    /*
     * Keep the cabinet, ALL+1, number and GO artwork pixel-identical between
     * states.  The two source strips have slightly different framing, so
     * swapping the entire pending strip makes the stationary controls appear
     * to jump.  Start from the normal strip and replace only the two cyan
     * button faces when the -/+ controls are active.
     */
    for (int row=0;row<FRUIT_CONTROLS_IMAGE_HEIGHT;++row) {
        memcpy(&pixels[(155+row)*SCREEN_W],
               &fruit_controls_image_rgb565[
                   row*FRUIT_CONTROLS_IMAGE_WIDTH],
               FRUIT_CONTROLS_IMAGE_WIDTH*sizeof(uint16_t));
    }

    if (s_state==STATE_PENDING_WIN) {
        int button_x=widths[0];
        for (int index=1;index<=2;++index) {
            int width=widths[index];
            for (int local_y=0;local_y<FRUIT_CONTROLS_IMAGE_HEIGHT;++local_y) {
                for (int local_x=0;local_x<width;++local_x) {
                    if (!control_button_shape_contains(
                            index,local_x,local_y,width)) continue;
                    int x=button_x+local_x;
                    pixels[(155+local_y)*SCREEN_W+x]=
                        fruit_controls_pending_image_rgb565[
                            local_y*FRUIT_CONTROLS_PENDING_IMAGE_WIDTH+x];
                }
            }
            button_x+=width;
        }
    }

    int x = 0;
    for (int i=0;i<6;++i) {
        int control=CONTROL_ALL+i;
        int w=widths[i];
        bool selected=s_selected_control==control;
        if (selected) {
            draw_control_selection_outline(i,x,w);
        }
        x += w;
    }
}

static void draw_bets(void)
{
    static const uint8_t symbol_tiles[FRUIT_CATEGORY_COUNT] = {
        2, 15, 19, 7, 1, 6, 0, 4
    };
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        int x=(i%4)*34;
        int y=195+(i/4)*22;
        int w=(i%4==3)?33:34;
        bool selected=s_selected_control==i;
        fill_rect(x,y,w,22,selected?0x542b0d:0x122218);
        rect(x,y,w,22,0x3d633f);
        if (i==FRUIT_SYMBOL_SEVEN || i==FRUIT_SYMBOL_STAR) {
            /*
             * These two cells need their source divider removed: it flattens
             * the top of 99 and appears as a stray line above the diamonds.
             */
            blit_bet_tile_without_top_divider(symbol_tiles[i],x+1,y+2);
        } else if (i==FRUIT_SYMBOL_MELON || i>=FRUIT_SYMBOL_BELL) {
            /*
             * Use the same complete 19x19 button area and alignment as 99 and
             * the diamonds. Do not rescale or offset these detailed symbols:
             * doing so drops edge samples and hides crowns, leaves and fruit.
             */
            blit_track_tile(symbol_tiles[i],x+1,y+2);
        } else {
            /*
             * Keep the complete SUP artwork inset from the button rim.
             */
            blit_bet_tile_inset(symbol_tiles[i],x+2,y+3);
        }
        if (i==FRUIT_SYMBOL_BAR) {
            /* The bet button is a plain SUP mark, without track-cell trim. */
            fill_rect(x+1,y+2,19,19,0x65166f);
            centered_text("SUP",x+10,y+8,1,0xffffff);
        }
        draw_bet_number(s_bets[i],x+26,y+9,
                        s_bets[i]?0xffffff:0x687468);
    }
}

static void draw_selected_control_cursor(void)
{
    /* The photographic ALL+1 row keeps only its original white silhouette. */
    if (s_selected_control>=CONTROL_ALL) {
        return;
    }
    int column=s_selected_control%4;
    int row=s_selected_control/4;
    int x=column*34;
    int y=195+row*22;
    int w=21;
    int h=22;

    /* A thin yellow cursor stays on the symbol and clears the bet box. */
    rect(x,y,w,h,0xffd700);
}

static void render(void)
{
    fill_rect(0,0,SCREEN_W,SCREEN_H,s_flash_red?0x4b0505:0x090705);
    if (s_gem_gallery) {
        draw_gem_gallery();
        lv_obj_invalidate(s_canvas);
        return;
    }
    draw_header();
    draw_track();
    draw_center();
    draw_inner_led_ring();
    draw_lucky_meter();
    draw_controls();
    draw_bets();
    draw_selected_control_cursor();
    draw_feedback_message();
    lv_obj_invalidate(s_canvas);
}

static esp_err_t write_stats_snapshot(const saved_stats_t *snapshot)
{
    nvs_handle_t handle;
    esp_err_t err=nvs_open("fruit88",NVS_READWRITE,&handle);
    if (err!=ESP_OK) return err;
    nvs_set_i32(handle,"credit",snapshot->credit);
    nvs_set_i32(handle,"high",snapshot->high_credit);
    nvs_set_u32(handle,"rounds",snapshot->total_rounds);
    nvs_set_u32(handle,"wins",snapshot->winning_rounds);
    nvs_set_i32(handle,"bestwin",snapshot->highest_single_win);
    /* Gem activation follows current CREDIT; discard the legacy accumulator. */
    nvs_erase_key(handle,"gemwon");
    nvs_set_u32(handle,"settings",snapshot->settings_flags);
    nvs_set_u8(handle,"free",snapshot->free_spins);
    nvs_set_u8(handle,"bbarmed",snapshot->big_bang_armed);
    nvs_set_u8(handle,"balancev",snapshot->balance_epoch);
    nvs_set_u8(handle,"gemlvl",snapshot->gem_level);
    nvs_set_blob(handle,"bets",snapshot->bets,sizeof(snapshot->bets));
    nvs_set_blob(handle,"prepaid",snapshot->prepaid_bets,
                 sizeof(snapshot->prepaid_bets));
    err=nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void mark_stats_dirty(void)
{
    s_stats_dirty=true;
    if (s_stats_flush_ms==0) {
        s_stats_flush_ms=now_ms()+fruit_game_tuning.nvs_flush_delay_ms;
    }
}

static void load_stats(void)
{
    nvs_handle_t handle;
    if (nvs_open("fruit88",NVS_READONLY,&handle)!=ESP_OK) return;
    nvs_get_i32(handle,"credit",&s_stats.credit);
    nvs_get_i32(handle,"high",&s_stats.high_credit);
    nvs_get_u32(handle,"rounds",&s_stats.total_rounds);
    nvs_get_u32(handle,"wins",&s_stats.winning_rounds);
    nvs_get_i32(handle,"bestwin",&s_stats.highest_single_win);
    nvs_get_u32(handle,"settings",&s_stats.settings_flags);
    nvs_get_u8(handle,"free",&s_stats.free_spins);
    nvs_get_u8(handle,"bbarmed",&s_stats.big_bang_armed);
    nvs_get_u8(handle,"balancev",&s_stats.balance_epoch);
    bool has_gem_level=nvs_get_u8(handle,"gemlvl",&s_stats.gem_level)==ESP_OK;
    size_t bets_size=sizeof(s_stats.bets);
    nvs_get_blob(handle,"bets",s_stats.bets,&bets_size);
    size_t prepaid_size=sizeof(s_stats.prepaid_bets);
    nvs_get_blob(handle,"prepaid",s_stats.prepaid_bets,&prepaid_size);
    nvs_close(handle);
    if (s_stats.balance_epoch<1) {
        s_stats.credit=0;
        s_stats.balance_epoch=1;
        s_stats_dirty=true;
        s_stats_flush_ms=now_ms()+fruit_game_tuning.nvs_flush_delay_ms;
        ESP_LOGI(TAG,"credit reset migration applied balance_epoch=1");
    }
    if (s_stats.credit<FRUIT_MIN_CREDIT ||
        s_stats.credit>FRUIT_MAX_CREDIT) {
        s_stats.credit=0;
    }
    if (s_stats.high_credit<0 || s_stats.high_credit>FRUIT_MAX_CREDIT) {
        s_stats.high_credit=s_stats.credit>0?s_stats.credit:0;
    }
    if (!has_gem_level || s_stats.balance_epoch<4 ||
        s_stats.gem_level<1 || s_stats.gem_level>30) {
        /*
         * A permanently lit gem records a threshold that the live CREDIT
         * balance reached at least once. The saved high-water balance is the
         * compatible source for that state; it is not a sum of spent credit.
         */
        s_stats.gem_level=gem_level_from_credit(s_stats.high_credit);
        s_stats.balance_epoch=4;
        s_stats_dirty=true;
        s_stats_flush_ms=now_ms()+fruit_game_tuning.nvs_flush_delay_ms;
        ESP_LOGI(TAG,"gem activation migrated level=%u high_credit=%ld",
                 s_stats.gem_level,(long)s_stats.high_credit);
    }
    if (s_stats.high_credit<s_stats.credit) s_stats.high_credit=s_stats.credit;
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        if (s_stats.bets[i]>FRUIT_MAX_BET_PER_SYMBOL) s_stats.bets[i]=0;
        if (s_stats.prepaid_bets[i]>s_stats.bets[i]) {
            s_stats.prepaid_bets[i]=0;
        }
        s_bets[i]=s_stats.bets[i];
        s_prepaid_bets[i]=s_stats.prepaid_bets[i];
    }
    s_free_spins=s_stats.free_spins;
    s_big_bang_armed=s_stats.big_bang_armed!=0;
}

static void record_award(int award)
{
    s_last_award=award;
    if (award>0) {
        ++s_stats.winning_rounds;
        if (award>s_stats.highest_single_win) s_stats.highest_single_win=award;
    }
}

static void credit_add(int amount)
{
    int64_t value=(int64_t)s_stats.credit+amount;
    if (value>FRUIT_MAX_CREDIT) value=FRUIT_MAX_CREDIT;
    if (value<FRUIT_MIN_CREDIT) value=FRUIT_MIN_CREDIT;
    s_stats.credit=(int)value;
    if (s_stats.credit>s_stats.high_credit) s_stats.high_credit=s_stats.credit;
    uint8_t reached_level=gem_level_after_credit(s_stats.gem_level,
                                                  s_stats.credit);
    if (reached_level>s_stats.gem_level) {
        s_stats.gem_level=reached_level;
        ESP_LOGI(TAG,"gem activated level=%u name=%s credit=%ld",
                 reached_level,s_gem_names[reached_level-1],
                 (long)s_stats.credit);
    }
}

static void credit_add_winnings(int amount)
{
    int old_credit=s_stats.credit;
    credit_add(amount);
    if (amount<=0) return;
    ESP_LOGI(TAG,"credit award=%d before=%d after=%ld gem=%u",
             amount,old_credit,(long)s_stats.credit,s_stats.gem_level);
}

static void add_credit_unit(void)
{
    if (s_state!=STATE_IDLE || s_stats.credit>=FRUIT_MAX_CREDIT) return;
    credit_add(FRUIT_CREDIT_TOPUP_UNIT);
    s_message=MSG_CREDIT_ADDED;
    mark_stats_dirty();
}

static void reset_credit(void)
{
    if (s_state!=STATE_IDLE) {
        ESP_LOGW(TAG,"credit reset ignored state=%d",s_state);
        return;
    }
    int old_credit=s_stats.credit;
    s_stats.credit=0;
    s_lucky_loss_count=0;
    s_lucky_spin_active=false;
    s_big_bang_armed=true;
    s_stats.big_bang_armed=1;
    s_message=MSG_CREDIT_RESET;
    mark_stats_dirty();
    play_sound(FRUIT_SOUND_START);
    ESP_LOGI(TAG,"credit reset old=%d new=0 by side-four-click",old_credit);
}

static void update_gamble_amount(void)
{
    if (s_pending_win<=0) {
        s_gamble_amount=0;
        return;
    }
    int percent=fruit_gamble_percentages[s_gamble_level];
    s_gamble_amount=(s_pending_win*percent+99)/100;
    if (s_gamble_amount<1) s_gamble_amount=1;
    if (s_gamble_amount>s_pending_win) s_gamble_amount=s_pending_win;
}

static void enter_gamble_or_settle(int award)
{
    record_award(award);
    if (s_current_spin_free) {
        credit_add_winnings(award);
        s_pending_win=0;
        mark_stats_dirty();
        s_message=award?MSG_WIN:MSG_MISS;
        s_state=STATE_IDLE;
        s_auto_spin_ms=now_ms()+700;
        return;
    }
    if (award>0) {
        s_pending_win=award;
        s_gamble_level=FRUIT_GAMBLE_LEVEL_COUNT-1;
        update_gamble_amount();
        s_guess_big=false;
        s_result_number=0;
        s_selected_control=CONTROL_SMALL;
        s_state=STATE_PENDING_WIN;
        s_message=MSG_GAMBLE;
    } else {
        s_pending_win=0;
        s_state=STATE_IDLE;
        s_message=MSG_MISS;
        mark_stats_dirty();
    }
}

static void play_stop_result_sound(int award, int multiplier)
{
    if (award<=0) {
        play_sound(FRUIT_SOUND_LOSE);
    } else if (multiplier<=15) {
        play_sound(FRUIT_SOUND_WIN_LOW);
    } else if (multiplier<=30) {
        play_sound(FRUIT_SOUND_WIN_MEDIUM);
    } else if (multiplier<=60) {
        play_sound(FRUIT_SOUND_WIN_HIGH);
    } else {
        play_sound(FRUIT_SOUND_WIN_TOP);
    }
}

static void settle_round_once(int award, int multiplier)
{
    if (s_round_settled) {
        ESP_LOGW(TAG,"duplicate round settlement ignored award=%d",award);
        return;
    }
    s_round_settled=true;
    int paid_stake=s_current_spin_free?0:s_last_stake;
    s_last_net=award-paid_stake;
    if (!s_current_spin_free) {
        if (award>s_last_stake || s_lucky_spin_active) {
            s_lucky_loss_count=0;
        } else if (s_lucky_loss_count<4) {
            ++s_lucky_loss_count;
        }
    }
    ESP_LOGI(TAG,
             "settle once award=%d stake=%d net=%d effective_multiplier=%d lucky_lamps=%u lucky_spin=%d",
             award,paid_stake,s_last_net,multiplier,
             (unsigned)s_lucky_loss_count,s_lucky_spin_active);
    play_stop_result_sound(award,multiplier);
    enter_gamble_or_settle(award);
}

static void finish_bonus_chain(void)
{
    settle_round_once(s_bonus_award,s_bonus_peak_multiplier);
}

static void start_bonus_chain(void)
{
    uint8_t span=fruit_game_tuning.blue_luck_max_cells-
        fruit_game_tuning.blue_luck_min_cells+1;
    s_bonus_steps=fruit_game_tuning.blue_luck_min_cells+(esp_random()%span);
    s_bonus_award=0;
    s_bonus_step_award=0;
    s_bonus_peak_multiplier=0;
    s_next_step_ms=now_ms()+fruit_game_tuning.blue_luck_step_ms;
    s_state=STATE_BONUS_CHAIN;
    s_message=MSG_LUCK_RIGHT;
    play_sound(FRUIT_SOUND_BONUS);
    ESP_LOGI(TAG,"five-light start cells=%d",s_bonus_steps);
}

static int weighted_luck_multiplier(void)
{
    uint32_t total=0;
    for (int i=0;i<fruit_luck_multiplier_count;++i) {
        total+=fruit_luck_multiplier_weights[i];
    }
    uint32_t draw=esp_random()%total;
    for (int i=0;i<fruit_luck_multiplier_count;++i) {
        if (draw<fruit_luck_multiplier_weights[i]) {
            return fruit_luck_multipliers[i];
        }
        draw-=fruit_luck_multiplier_weights[i];
    }
    return fruit_luck_multipliers[0];
}

static void finish_spin(void)
{
    const fruit_track_cell_t *cell=&fruit_track[s_highlight];
    ++s_stats.total_rounds;
    s_result_number=s_highlight;
    if (cell->luck==FRUIT_LUCK_ORANGE) {
        int multiplier=weighted_luck_multiplier();
        int award=s_last_stake*multiplier;
        s_message=MSG_LUCK_LEFT;
        ESP_LOGI(TAG,"left LUCK stake=%d multiplier=%d award=%d",
                 s_last_stake,multiplier,award);
        settle_round_once(award,multiplier);
    } else if (cell->luck==FRUIT_LUCK_BLUE) {
        start_bonus_chain();
    } else {
        settle_round_once(cell_award(s_highlight),
                          cell_effective_multiplier(s_highlight));
    }
    ESP_LOGI(TAG,"result cell=%d symbol=%d x%d award=%d free=%d",
             s_highlight,cell->symbol,cell->multiplier,s_last_award,
             s_current_spin_free);
}

static int weighted_target(void)
{
    uint32_t total=0;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) total+=fruit_track[i].weight;
    uint32_t draw=esp_random()%total;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        if (draw<fruit_track[i].weight) return i;
        draw-=fruit_track[i].weight;
    }
    return 0;
}

static int weighted_fruit_target(void)
{
    uint32_t total=0;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        if (fruit_track[i].symbol!=FRUIT_SYMBOL_LUCK) {
            total+=fruit_track[i].weight;
        }
    }
    uint32_t draw=esp_random()%total;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        if (fruit_track[i].symbol==FRUIT_SYMBOL_LUCK) continue;
        if (draw<fruit_track[i].weight) return i;
        draw-=fruit_track[i].weight;
    }
    return 0;
}

static int weighted_profitable_target(int stake)
{
    int nearest_award=INT_MAX;
    uint32_t total=0;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        if (fruit_track[i].luck!=FRUIT_LUCK_NONE) continue;
        int award=cell_award(i);
        if (award<=stake) continue;
        if (award<nearest_award) {
            nearest_award=award;
            total=fruit_track[i].weight;
        } else if (award==nearest_award) {
            total+=fruit_track[i].weight;
        }
    }
    if (total>0) {
        uint32_t draw=esp_random()%total;
        for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
            if (fruit_track[i].luck!=FRUIT_LUCK_NONE ||
                cell_award(i)!=nearest_award) continue;
            if (draw<fruit_track[i].weight) return i;
            draw-=fruit_track[i].weight;
        }
    }
    /* The orange WOW is a guaranteed fallback: its minimum is 5 x stake. */
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        if (fruit_track[i].luck==FRUIT_LUCK_ORANGE) return i;
    }
    return weighted_target();
}

static esp_err_t run_logic_self_test(void)
{
    static const uint16_t expected_payouts[FRUIT_CATEGORY_COUNT] = {
        0, 40, 30, 20, 25, 15, 10, 5,
    };
    static const uint8_t expected_luck_multipliers[] = {
        5, 10, 15, 20, 30, 40, 60,
    };
    static const uint8_t expected_luck_weights[] = {
        40, 25, 15, 10, 5, 3, 2,
    };
    if (gem_level_from_credit(99)!=1 ||
        gem_level_from_credit(100)!=2 ||
        gem_level_from_credit(43500)!=30 ||
        gem_level_after_credit(3,0)!=3 ||
        gem_level_after_credit(3,-500)!=3) {
        ESP_LOGE(TAG,"current-credit gem rule invalid");
        return ESP_ERR_INVALID_ARG;
    }
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        if (fruit_base_payouts[i]!=expected_payouts[i]) {
            ESP_LOGE(TAG,"payout config invalid category=%d",i);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (fruit_luck_multiplier_count!=sizeof(expected_luck_multipliers) ||
        memcmp(fruit_luck_multipliers,expected_luck_multipliers,
               sizeof(expected_luck_multipliers))!=0 ||
        memcmp(fruit_luck_multiplier_weights,expected_luck_weights,
               sizeof(expected_luck_weights))!=0 ||
        fruit_game_tuning.blue_luck_step_ms!=650 ||
        fruit_game_tuning.blue_luck_min_cells!=5 ||
        fruit_game_tuning.blue_luck_max_cells!=5) {
        ESP_LOGE(TAG,"LUCK config invalid");
        return ESP_ERR_INVALID_ARG;
    }

    size_t heap_before=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    for (int round=0;round<1000;++round) {
        int target=weighted_target();
        int start=round%FRUIT_TRACK_COUNT;
        int fixed=fruit_game_tuning.spin_accel_steps+
            fruit_game_tuning.spin_steady_laps*FRUIT_TRACK_COUNT+
            fruit_game_tuning.spin_decel_min_steps+
            round%FRUIT_TRACK_COUNT;
        int delta=(target-start+FRUIT_TRACK_COUNT)%FRUIT_TRACK_COUNT;
        int steps=fixed+
            (delta-(fixed%FRUIT_TRACK_COUNT)+FRUIT_TRACK_COUNT)%
                FRUIT_TRACK_COUNT;
        if ((start+steps)%FRUIT_TRACK_COUNT!=target ||
            steps<fruit_game_tuning.spin_accel_steps+
                2*FRUIT_TRACK_COUNT+
                fruit_game_tuning.spin_decel_min_steps) {
            ESP_LOGE(TAG,"1000-round self-test failed at round=%d",round);
            return ESP_FAIL;
        }
    }
    int bar50_count=0;
    int bar100_count=0;
    int left_luck_count=0;
    int right_luck_count=0;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        const fruit_track_cell_t *cell=&fruit_track[i];
        if (cell->symbol>FRUIT_SYMBOL_LUCK || cell->weight==0 ||
            (cell->multiplier!=1 && cell->multiplier!=3)) {
            ESP_LOGE(TAG,"track config invalid at cell=%d",i);
            return ESP_ERR_INVALID_ARG;
        }
        if (cell->symbol==FRUIT_SYMBOL_BAR) {
            if (cell->payout_override==50) ++bar50_count;
            else if (cell->payout_override==100) ++bar100_count;
            else {
                ESP_LOGE(TAG,"BAR payout invalid at cell=%d",i);
                return ESP_ERR_INVALID_ARG;
            }
        }
        if (cell->luck==FRUIT_LUCK_ORANGE) ++left_luck_count;
        if (cell->luck==FRUIT_LUCK_BLUE) ++right_luck_count;
    }
    if (bar50_count!=1 || bar100_count!=1 ||
        left_luck_count!=1 || right_luck_count!=1) {
        ESP_LOGE(TAG,"special track config invalid");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t saved_bets[FRUIT_CATEGORY_COUNT];
    memcpy(saved_bets,s_bets,sizeof(saved_bets));
    memset(s_bets,1,sizeof(s_bets));
    uint32_t total_weight=0;
    uint32_t all_fixed_weighted_award=0;
    uint32_t all_net_win_weight=0;
    uint32_t left_luck_weight=0;
    uint32_t right_luck_weight=0;
    uint32_t fruit_weight=0;
    uint32_t luck_weight_sum=0;
    uint32_t luck_weighted_multiplier_sum=0;
    int all_stake=total_bet();
    for (int i=0;i<fruit_luck_multiplier_count;++i) {
        luck_weight_sum+=fruit_luck_multiplier_weights[i];
        luck_weighted_multiplier_sum+=fruit_luck_multipliers[i]*
            fruit_luck_multiplier_weights[i];
    }
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        const fruit_track_cell_t *cell=&fruit_track[i];
        total_weight+=cell->weight;
        int expected=0;
        if (cell->symbol!=FRUIT_SYMBOL_LUCK) {
            int base=cell->payout_override?cell->payout_override:
                fruit_base_payouts[cell->symbol];
            expected=base*cell->multiplier;
        }
        if (cell_award(i)!=expected) {
            memcpy(s_bets,saved_bets,sizeof(s_bets));
            ESP_LOGE(TAG,"award rule invalid at cell=%d",i);
            return ESP_ERR_INVALID_ARG;
        }
        if (cell->luck==FRUIT_LUCK_BLUE) {
            right_luck_weight+=cell->weight;
            all_net_win_weight+=cell->weight;
            continue;
        } else if (cell->luck==FRUIT_LUCK_ORANGE) {
            left_luck_weight+=cell->weight;
            all_net_win_weight+=cell->weight;
            continue;
        }
        fruit_weight+=cell->weight;
        all_fixed_weighted_award+=cell->weight*expected;
        if (expected>all_stake) all_net_win_weight+=cell->weight;
    }
    /*
     * Left WOW uses its configured weighted multiplier distribution. Right
     * WOW performs five independent fruit-only draws, redrawing WOW results.
     * Keep the expectation calculation integer with a shared denominator.
     */
    uint64_t expected_award_numerator=
        (uint64_t)all_fixed_weighted_award*luck_weight_sum*fruit_weight+
        (uint64_t)left_luck_weight*all_stake*
            luck_weighted_multiplier_sum*fruit_weight+
        (uint64_t)right_luck_weight*5*all_fixed_weighted_award*
            luck_weight_sum;
    uint64_t expected_award_denominator=
        (uint64_t)total_weight*luck_weight_sum*fruit_weight;
    uint32_t all_rtp_basis_points=(uint32_t)(
        expected_award_numerator*10000ULL/
        (expected_award_denominator*all_stake));
    uint32_t net_win_basis_points=
        all_net_win_weight*10000/total_weight;
    int nearest_profitable_award=INT_MAX;
    for (int i=0;i<FRUIT_TRACK_COUNT;++i) {
        if (fruit_track[i].luck!=FRUIT_LUCK_NONE) continue;
        int award=cell_award(i);
        if (award>all_stake && award<nearest_profitable_award) {
            nearest_profitable_award=award;
        }
    }
    for (int draw=0;draw<100;++draw) {
        int target=weighted_profitable_target(all_stake);
        if (fruit_track[target].luck!=FRUIT_LUCK_NONE ||
            cell_award(target)!=nearest_profitable_award) {
            memcpy(s_bets,saved_bets,sizeof(s_bets));
            ESP_LOGE(TAG,"lucky target invalid target=%d award=%d",
                     target,cell_award(target));
            return ESP_ERR_INVALID_ARG;
        }
    }
    /*
     * Four consecutive non-net wins arm the fifth paid spin. The lucky spin
     * chooses the nearest profitable ordinary result, keeping the protection
     * useful without turning the long-run return into an uncontrolled jackpot.
     */
    uint64_t non_win_weight=total_weight-all_net_win_weight;
    uint64_t normal_state_weight=0;
    uint64_t state_term=1;
    for (int state=0;state<4;++state) {
        uint64_t total_power=1;
        for (int p=state;p<4;++p) total_power*=total_weight;
        normal_state_weight+=state_term*total_power;
        state_term*=non_win_weight;
    }
    uint64_t lucky_state_weight=state_term;
    uint64_t all_state_weight=normal_state_weight+lucky_state_weight;
    uint32_t lucky_rtp_basis_points=
        nearest_profitable_award*10000/all_stake;
    uint32_t protected_rtp_basis_points=(uint32_t)(
        ((uint64_t)all_rtp_basis_points*normal_state_weight+
         (uint64_t)lucky_rtp_basis_points*lucky_state_weight)/
        all_state_weight);
    uint32_t protected_net_win_basis_points=(uint32_t)(
        ((uint64_t)net_win_basis_points*normal_state_weight+
         10000ULL*lucky_state_weight)/all_state_weight);
    if (total_weight!=60 || fruit_weight!=58 || luck_weight_sum!=100 ||
        all_rtp_basis_points<10000 || all_rtp_basis_points>11000 ||
        protected_rtp_basis_points<10300 ||
        protected_rtp_basis_points>10600 ||
        net_win_basis_points<2500 || net_win_basis_points>3500 ||
        protected_net_win_basis_points<3700 ||
        protected_net_win_basis_points>4000) {
        memcpy(s_bets,saved_bets,sizeof(s_bets));
        ESP_LOGE(TAG,
                 "balance invalid weight=%lu base_rtp_bp=%lu protected_rtp_bp=%lu base_net_win_bp=%lu protected_net_win_bp=%lu",
                 (unsigned long)total_weight,
                 (unsigned long)all_rtp_basis_points,
                 (unsigned long)protected_rtp_basis_points,
                 (unsigned long)net_win_basis_points,
                 (unsigned long)protected_net_win_basis_points);
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_bets,saved_bets,sizeof(s_bets));
    size_t heap_after=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG,
             "1000-round logic self-test PASS heap_delta=%d weight=%lu base_rtp_bp=%lu protected_rtp_bp=%lu base_net_win_bp=%lu protected_net_win_bp=%lu",
             (int)heap_after-(int)heap_before,
             (unsigned long)total_weight,
             (unsigned long)all_rtp_basis_points,
             (unsigned long)protected_rtp_basis_points,
             (unsigned long)net_win_basis_points,
             (unsigned long)protected_net_win_basis_points);
    return ESP_OK;
}

static void start_spin(bool free_spin)
{
    if (s_state!=STATE_IDLE) {
        ESP_LOGI(TAG,"spin blocked state=%d",s_state);
        return;
    }
    int stake=total_bet();
    if (stake<=0) {
        ESP_LOGI(TAG,"spin blocked no bet");
        s_message=MSG_NO_BET; render(); return;
    }
    if (!free_spin) {
        int remaining_cost=stake-prepaid_bet_cost();
        if (remaining_cost>0) credit_add(-remaining_cost);
        memset(s_prepaid_bets,0,sizeof(s_prepaid_bets));
    }
    else if (s_free_spins>0) --s_free_spins;
    s_current_spin_free=free_spin;
    s_lucky_spin_active=!free_spin && s_lucky_loss_count>=4;
    s_round_settled=false;
    s_last_stake=stake;
    s_last_award=0;
    s_last_net=0;
    s_pending_win=0; s_result_number=0;
    s_spin_target=s_lucky_spin_active?
        weighted_profitable_target(stake):weighted_target();
    int fixed_steps=fruit_game_tuning.spin_accel_steps+
        fruit_game_tuning.spin_steady_laps*FRUIT_TRACK_COUNT+
        fruit_game_tuning.spin_decel_min_steps+
        esp_random()%FRUIT_TRACK_COUNT;
    int delta=(s_spin_target-s_highlight+FRUIT_TRACK_COUNT)%FRUIT_TRACK_COUNT;
    s_spin_total=fixed_steps+
        (delta-(fixed_steps%FRUIT_TRACK_COUNT)+FRUIT_TRACK_COUNT)%
            FRUIT_TRACK_COUNT;
    s_spin_steps=s_spin_total;
    s_led_phase=0;
    s_next_step_ms=now_ms()+fruit_game_tuning.spin_start_interval_ms;
    s_state=STATE_SPIN;
    s_message=free_spin?MSG_FREE_SPIN:MSG_SPIN;
    play_sound(FRUIT_SOUND_START);
    mark_stats_dirty();
    ESP_LOGI(TAG,
             "spin start stake=%d free=%d target=%d credit=%ld lucky_lamps=%u lucky_spin=%d",
             stake,free_spin,s_spin_target,(long)s_stats.credit,
             (unsigned)s_lucky_loss_count,s_lucky_spin_active);
    render();
}

static void begin_big_bang(void)
{
    s_big_bang_armed=false;
    s_state=STATE_BIG_BANG;
    s_flash_steps=fruit_game_tuning.big_bang_flash_steps;
    s_fire_phase=0;
    s_flash_red=true;
    s_free_spins=fruit_game_tuning.big_bang_free_spins;
    s_next_step_ms=now_ms()+fruit_game_tuning.big_bang_flash_interval_ms;
    s_message=MSG_BIG_BANG;
    play_sound(FRUIT_SOUND_BIG_BANG);
    mark_stats_dirty();
    ESP_LOGI(TAG,"BIG BANG threshold=%d credit=%ld",
             FRUIT_BIG_BANG_THRESHOLD,(long)s_stats.credit);
}

static void bank_pending(void)
{
    if (s_state!=STATE_PENDING_WIN) return;
    int banked=s_pending_win;
    s_last_award=banked;
    s_last_net=banked-s_last_stake;
    if (banked>s_stats.highest_single_win) {
        s_stats.highest_single_win=banked;
    }
    credit_add_winnings(banked);
    s_pending_win=0; s_gamble_amount=0; s_state=STATE_IDLE;
    s_message=MSG_WIN;
    mark_stats_dirty();
    play_sound(FRUIT_SOUND_WIN);
    if (s_stats.credit>=FRUIT_BIG_BANG_THRESHOLD && s_big_bang_armed) {
        begin_big_bang();
    }
    ESP_LOGI(TAG,"collect=%d credit=%ld",banked,(long)s_stats.credit);
}

static void gamble(void)
{
    if (s_state!=STATE_PENDING_WIN ||
        s_pending_win<=0 || s_gamble_amount<=0) return;
    s_result_number=1+(esp_random()%13);
    bool correct=s_result_number!=7 &&
        ((s_result_number<=6 && !s_guess_big) ||
         (s_result_number>=8 && s_guess_big));
    int safe=s_pending_win-s_gamble_amount;
    if (correct) {
        s_pending_win=safe+s_gamble_amount*2;
        if (s_pending_win>FRUIT_MAX_CREDIT) s_pending_win=FRUIT_MAX_CREDIT;
        update_gamble_amount();
        s_message=MSG_GAMBLE_WIN;
        play_sound(FRUIT_SOUND_BONUS);
    } else {
        s_pending_win=safe;
        update_gamble_amount();
        s_message=MSG_GAMBLE_LOSE;
        play_sound(FRUIT_SOUND_LOSE);
        if (s_pending_win<=0) {
            s_state=STATE_IDLE;
            mark_stats_dirty();
        }
    }
    ESP_LOGI(TAG,"gamble n=%d guess=%s correct=%d pending=%d",
             s_result_number,s_guess_big?"big":"small",correct,s_pending_win);
}

static void add_bet(int category)
{
    if (category<0 || category>=FRUIT_CATEGORY_COUNT) return;
    if (s_bets[category]>=FRUIT_MAX_BET_PER_SYMBOL) return;
    ++s_bets[category];
    ++s_prepaid_bets[category];
    credit_add(-fruit_bet_costs[category]);
    s_message=MSG_READY;
    mark_stats_dirty();
    ESP_LOGI(TAG,"bet add category=%d cost=%u units=%u credit=%ld",
             category,(unsigned)fruit_bet_costs[category],
             (unsigned)s_bets[category],(long)s_stats.credit);
}

static void add_all(void)
{
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        if (s_bets[i]>=FRUIT_MAX_BET_PER_SYMBOL) return;
    }
    int cost=0;
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        ++s_bets[i];
        ++s_prepaid_bets[i];
        cost+=fruit_bet_costs[i];
    }
    credit_add(-cost);
    s_message=MSG_READY;
    mark_stats_dirty();
    ESP_LOGI(TAG,"bet add all cost=%d credit=%ld",cost,
             (long)s_stats.credit);
}

static void double_bets(void)
{
    int doubled_total=0;
    int added_cost=0;
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        int doubled=s_bets[i]*2;
        if (doubled>FRUIT_MAX_BET_PER_SYMBOL) {
            doubled=FRUIT_MAX_BET_PER_SYMBOL;
        }
        doubled_total+=doubled;
    }
    if (doubled_total==0) {
        s_message=MSG_NO_BET;
        return;
    }
    for (int i=0;i<FRUIT_CATEGORY_COUNT;++i) {
        int doubled=s_bets[i]*2;
        int capped=doubled>FRUIT_MAX_BET_PER_SYMBOL
            ?FRUIT_MAX_BET_PER_SYMBOL:doubled;
        int added=capped-s_bets[i];
        s_bets[i]=capped;
        s_prepaid_bets[i]+=added;
        added_cost+=added*fruit_bet_costs[i];
    }
    credit_add(-added_cost);
    s_message=MSG_READY;
    mark_stats_dirty();
    ESP_LOGI(TAG,"bet double added_cost=%d total_cost=%d credit=%ld",
             added_cost,total_bet(),(long)s_stats.credit);
}

static void clear_all(void)
{
    int refund=prepaid_bet_cost();
    credit_add(refund);
    memset(s_bets,0,sizeof(s_bets));
    memset(s_prepaid_bets,0,sizeof(s_prepaid_bets));
    s_message=MSG_CLEARED;
    mark_stats_dirty();
    ESP_LOGI(TAG,"bet clear all refund=%d credit=%ld",
             refund,(long)s_stats.credit);
}

static void decrease_bet(int category)
{
    if (category<0 || category>=FRUIT_CATEGORY_COUNT) return;
    if (s_bets[category]==0) {
        s_message=MSG_NO_BET;
        return;
    }
    int refund=0;
    if (s_prepaid_bets[category]>0) {
        --s_prepaid_bets[category];
        refund=fruit_bet_costs[category];
    }
    credit_add(refund);
    --s_bets[category];
    s_message=MSG_BET_DECREASED;
    mark_stats_dirty();
    ESP_LOGI(TAG,"bet decrease category=%d refund=%d units=%u credit=%ld",
             category,refund,(unsigned)s_bets[category],
             (long)s_stats.credit);
}

static void adjust_gamble_level(int delta)
{
    if (delta<0 && s_gamble_level>0) --s_gamble_level;
    else if (delta>0 && s_gamble_level+1<FRUIT_GAMBLE_LEVEL_COUNT) {
        ++s_gamble_level;
    }
    update_gamble_amount();
    s_message=MSG_GAMBLE;
}

static void cycle_pending_action(int delta)
{
    static const uint8_t actions[3] = {
        CONTROL_SMALL, CONTROL_BIG, CONTROL_GO
    };
    int current=0;
    for (int i=0;i<3;++i) {
        if (s_selected_control==actions[i]) {
            current=i;
            break;
        }
    }
    current=(current+delta+3)%3;
    s_selected_control=actions[current];
}

static void activate_selected(bool long_press)
{
    if (s_state==STATE_SPIN || s_state==STATE_BONUS_CHAIN ||
        s_state==STATE_BIG_BANG) return;
    int control=s_selected_control;
    if (s_state==STATE_PENDING_WIN) {
        if (control==CONTROL_LEFT) {
            adjust_gamble_level(-1);
        } else if (control==CONTROL_RIGHT) {
            adjust_gamble_level(1);
        } else if (control==CONTROL_SMALL || control==CONTROL_BIG) {
            s_guess_big=control==CONTROL_BIG; gamble();
        } else if (control==CONTROL_GO) {
            bank_pending();
        }
        return;
    }
    if (control<FRUIT_CATEGORY_COUNT) {
        if (long_press) {
            decrease_bet(control);
        } else add_bet(control);
    } else if (control==CONTROL_ALL) {
        if (long_press) {
            clear_all();
        } else add_all();
    } else if (control==CONTROL_LEFT) {
        double_bets();
    } else if (control==CONTROL_RIGHT) {
        clear_all();
    } else if (control==CONTROL_GO) start_spin(false);
}

static void update_game(void)
{
    int64_t now=now_ms();
    if (s_state==STATE_SPIN && now>=s_next_step_ms) {
        s_highlight=(s_highlight+1)%FRUIT_TRACK_COUNT;
        ++s_led_phase;
        --s_spin_steps;
        /*
         * Audio runs in its own task. During the fastest part, sound every
         * other cell; once deceleration is audible, sound every cell.
         */
        if (s_spin_steps < s_spin_total / 2 || (s_spin_steps & 1)==0) {
            play_sound(FRUIT_SOUND_TICK);
        }
        if (s_spin_steps<=0) {
            if (s_highlight!=s_spin_target) {
                ESP_LOGE(TAG,"spin target mismatch expected=%d actual=%d",
                         s_spin_target,s_highlight);
            }
            finish_spin();
        }
        else {
            int done=s_spin_total-s_spin_steps;
            int accel=fruit_game_tuning.spin_accel_steps;
            int steady=fruit_game_tuning.spin_steady_laps*FRUIT_TRACK_COUNT;
            int interval;
            if (done<accel) {
                int range=fruit_game_tuning.spin_start_interval_ms-
                    fruit_game_tuning.spin_fast_interval_ms;
                interval=fruit_game_tuning.spin_start_interval_ms-
                    range*(done+1)/accel;
            } else if (done<accel+steady) {
                interval=fruit_game_tuning.spin_fast_interval_ms;
            } else {
                int decel_done=done-accel-steady;
                int decel_total=s_spin_total-accel-steady;
                int range=fruit_game_tuning.spin_end_interval_ms-
                    fruit_game_tuning.spin_fast_interval_ms;
                interval=fruit_game_tuning.spin_fast_interval_ms+
                    range*decel_done*decel_done/(decel_total*decel_total);
            }
            s_next_step_ms=now+interval;
        }
        render();
    } else if (s_state==STATE_BONUS_CHAIN && now>=s_next_step_ms) {
        s_highlight=weighted_fruit_target();
        ++s_led_phase;
        int step_award=bonus_cell_award(s_highlight);
        s_bonus_step_award=step_award;
        int step_multiplier=cell_effective_multiplier(s_highlight);
        s_bonus_award+=step_award;
        if (step_award>0 && step_multiplier>s_bonus_peak_multiplier) {
            s_bonus_peak_multiplier=step_multiplier;
        }
        --s_bonus_steps;
        ESP_LOGI(TAG,
                 "five-light cell=%u symbol=%u award=%d total=%d remaining=%d",
                 (unsigned)s_highlight,
                 (unsigned)fruit_track[s_highlight].symbol,
                 step_award,s_bonus_award,s_bonus_steps);
        s_message=MSG_LUCK_RIGHT;
        if (s_bonus_steps<=0) finish_bonus_chain();
        else s_next_step_ms=now+fruit_game_tuning.blue_luck_step_ms;
        render();
    } else if (s_state==STATE_BIG_BANG && now>=s_next_step_ms) {
        ++s_fire_phase;
        s_flash_red=!s_flash_red;
        if (--s_flash_steps==0) {
            s_flash_red=false; s_state=STATE_IDLE;
            s_auto_spin_ms=now+300;
        } else {
            s_next_step_ms=now+fruit_game_tuning.big_bang_flash_interval_ms;
        }
        render();
    } else if (s_state==STATE_IDLE && s_free_spins>0 &&
               s_auto_spin_ms>0 && now>=s_auto_spin_ms) {
        s_auto_spin_ms=0; start_spin(true);
    }
    if (s_stats.credit<FRUIT_BIG_BANG_THRESHOLD && !s_big_bang_armed) {
        s_big_bang_armed=true;
        mark_stats_dirty();
    }
}

static int control_row(int control)
{
    if (control >= CONTROL_ALL) return 0;
    return control < 4 ? 1 : 2;
}

static int control_center_x(int control)
{
    static const uint8_t control_x[6] = {12, 35, 56, 76, 94, 120};
    static const uint8_t bet_x[4] = {17, 51, 85, 118};
    if (control >= CONTROL_ALL) return control_x[control - CONTROL_ALL];
    return bet_x[control % 4];
}

static void navigate_visual(int horizontal, int vertical)
{
    int current = s_selected_control;
    int row = control_row(current);
    if (horizontal != 0) {
        int best = current;
        int best_distance = 1000;
        int current_x = control_center_x(current);
        for (int candidate = 0; candidate < CONTROL_COUNT; ++candidate) {
            if (control_row(candidate) != row || candidate == current) continue;
            int delta = control_center_x(candidate) - current_x;
            if ((horizontal < 0 && delta < 0 && -delta < best_distance) ||
                (horizontal > 0 && delta > 0 && delta < best_distance)) {
                best = candidate;
                best_distance = delta < 0 ? -delta : delta;
            }
        }
        if (best==current) {
            best_distance=-1;
            for (int candidate=0;candidate<CONTROL_COUNT;++candidate) {
                if (control_row(candidate)!=row || candidate==current) continue;
                int delta=control_center_x(candidate)-current_x;
                int distance=delta<0?-delta:delta;
                if (distance>best_distance) {
                    best=candidate;
                    best_distance=distance;
                }
            }
        }
        s_selected_control = best;
        return;
    }
    if (vertical != 0) {
        int target_row = (row + vertical + 3) % 3;
        int best = current;
        int best_distance = 1000;
        int current_x = control_center_x(current);
        for (int candidate = 0; candidate < CONTROL_COUNT; ++candidate) {
            if (control_row(candidate) != target_row) continue;
            int distance = control_center_x(candidate) - current_x;
            if (distance < 0) distance = -distance;
            if (distance < best_distance) {
                best = candidate;
                best_distance = distance;
            }
        }
        s_selected_control = best;
    }
}

static void handle_event(game_event_t event)
{
    if (!s_lvgl_lock ||
        xSemaphoreTake(s_lvgl_lock,portMAX_DELAY)!=pdTRUE) return;
    uint8_t control_before=s_selected_control;
    ESP_LOGI(TAG,"input event=%d state=%d control=%u",
             event,s_state,s_selected_control);
    if (event==EVENT_TOGGLE_GEMS) {
        if (s_state!=STATE_SPIN && s_state!=STATE_BONUS_CHAIN &&
            s_state!=STATE_BIG_BANG) {
            s_gem_gallery=!s_gem_gallery;
            ESP_LOGI(TAG,"gem gallery %s level=%u credit=%ld",
                     s_gem_gallery?"open":"closed",
                     s_stats.gem_level,
                     (long)s_stats.credit);
            play_sound(FRUIT_SOUND_TICK);
        }
    } else if (s_gem_gallery) {
        ESP_LOGI(TAG,"input ignored while gem gallery is open");
    } else if (event==EVENT_NEXT && s_state!=STATE_SPIN &&
        s_state!=STATE_BONUS_CHAIN && s_state!=STATE_BIG_BANG) {
        s_selected_control=(s_selected_control+1)%CONTROL_COUNT;
    } else if (event==EVENT_PREVIOUS && s_state!=STATE_SPIN &&
               s_state!=STATE_BONUS_CHAIN && s_state!=STATE_BIG_BANG) {
        s_selected_control=(s_selected_control+CONTROL_COUNT-1)%CONTROL_COUNT;
    } else if (event==EVENT_GO) {
        if (s_state==STATE_PENDING_WIN) bank_pending();
        else start_spin(false);
    } else if (event==EVENT_ADD_CREDIT) {
        add_credit_unit();
    } else if (event==EVENT_RESET_CREDIT) {
        reset_credit();
    } else if (event==EVENT_ACTIVATE || event==EVENT_ACTIVATE_LONG) {
        activate_selected(event==EVENT_ACTIVATE_LONG);
    } else if (s_state==STATE_PENDING_WIN &&
               (event==EVENT_MOTION_LEFT || event==EVENT_MOTION_RIGHT)) {
        s_selected_control=event==EVENT_MOTION_LEFT
            ? CONTROL_LEFT : CONTROL_RIGHT;
        adjust_gamble_level(event==EVENT_MOTION_LEFT?-1:1);
    } else if (s_state==STATE_PENDING_WIN &&
               (event==EVENT_MOTION_UP || event==EVENT_MOTION_DOWN)) {
        cycle_pending_action(event==EVENT_MOTION_UP?-1:1);
    } else if (s_state!=STATE_SPIN && s_state!=STATE_BONUS_CHAIN &&
               s_state!=STATE_BIG_BANG) {
        if (event==EVENT_MOTION_LEFT) navigate_visual(-1,0);
        else if (event==EVENT_MOTION_RIGHT) navigate_visual(1,0);
        else if (event==EVENT_MOTION_UP) navigate_visual(0,-1);
        else if (event==EVENT_MOTION_DOWN) navigate_visual(0,1);
    }
    if (!s_gem_gallery &&
        event>=EVENT_MOTION_LEFT && event<=EVENT_MOTION_DOWN &&
        s_state!=STATE_SPIN && s_state!=STATE_BONUS_CHAIN &&
        s_state!=STATE_BIG_BANG) {
        play_sound(FRUIT_SOUND_TICK);
    }
    if (event>=EVENT_MOTION_LEFT && event<=EVENT_MOTION_DOWN) {
        ESP_LOGI(TAG,"motion applied control=%u->%u",
                 control_before,s_selected_control);
    }
    render();
    lv_refr_now(s_display);
    lvgl_unlock();
}

static void queue_event(game_event_t event)
{
    if (s_events && xQueueSend(s_events,&event,0)!=pdTRUE) {
        ESP_LOGW(TAG,"input queue full event=%d",event);
    }
}

static void front_single_cb(void *h,void *u)
{
    (void)h;(void)u;ESP_LOGI(TAG,"blue single");queue_event(EVENT_ACTIVATE);
}
static void front_repeat_cb(void *h,void *u)
{
    (void)h;(void)u;ESP_LOGI(TAG,"blue double second-down");queue_event(EVENT_GO);
}
static void front_long_cb(void *h,void *u)
{
    (void)h;(void)u;ESP_LOGI(TAG,"blue long");queue_event(EVENT_ACTIVATE_LONG);
}
static void side_single_cb(void *h,void *u)
{
    (void)h;(void)u;ESP_LOGI(TAG,"side single");queue_event(EVENT_ADD_CREDIT);
}
static void side_double_cb(void *h,void *u)
{
    (void)h;(void)u;ESP_LOGI(TAG,"side double");queue_event(EVENT_PREVIOUS);
}
static void side_release_cb(void *h,void *u)
{
    (void)h;(void)u;
    static uint64_t last_release_ms;
    static uint8_t click_count;
    uint64_t current_ms=now_ms();
    if (current_ms-last_release_ms>900) click_count=0;
    last_release_ms=current_ms;
    ++click_count;
    ESP_LOGI(TAG,"side reset click count=%u",click_count);
    if (click_count>=4) {
        click_count=0;
        ESP_LOGI(TAG,"side four click");
        queue_event(EVENT_RESET_CREDIT);
    }
}
static void side_long_cb(void *h,void *u)
{
    (void)h;(void)u;ESP_LOGI(TAG,"side long");queue_event(EVENT_TOGGLE_GEMS);
}

static esp_err_t init_buttons(void)
{
    button_handle_t front=NULL,side=NULL;
    const button_config_t config={.short_press_time=500,.long_press_time=900};
    const button_gpio_config_t fg={.gpio_num=PIN_BUTTON_FRONT,.active_level=0,
                                   .enable_power_save=false};
    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&config,&fg,&front),TAG,"front");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(front,BUTTON_SINGLE_CLICK,NULL,
        front_single_cb,NULL),TAG,"front single");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(front,BUTTON_PRESS_REPEAT,NULL,
        front_repeat_cb,NULL),TAG,"front repeat");
    button_event_args_t args={.long_press={.press_time=900}};
    ESP_RETURN_ON_ERROR(iot_button_register_cb(front,BUTTON_LONG_PRESS_START,&args,
        front_long_cb,NULL),TAG,"front long");
    const button_gpio_config_t sg={.gpio_num=PIN_BUTTON_SIDE,.active_level=0,
                                   .enable_power_save=false};
    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&config,&sg,&side),TAG,"side");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(side,BUTTON_SINGLE_CLICK,NULL,
        side_single_cb,NULL),TAG,"side single");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(side,BUTTON_DOUBLE_CLICK,NULL,
        side_double_cb,NULL),TAG,"side double");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(
        side,BUTTON_PRESS_UP,NULL,side_release_cb,NULL),TAG,"side release");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(side,BUTTON_LONG_PRESS_START,&args,
        side_long_cb,NULL),TAG,"side long");
    return ESP_OK;
}

static bool notify_flush(esp_lcd_panel_io_handle_t io,
                         esp_lcd_panel_io_event_data_t *data,void *ctx)
{
    (void)io;(void)data;lv_display_flush_ready((lv_display_t *)ctx);return false;
}

static void lvgl_flush_cb(lv_display_t *display,const lv_area_t *area,
                          uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel=lv_display_get_user_data(display);
    int32_t size=(area->x2-area->x1+1)*(area->y2-area->y1+1);
    lv_draw_sw_rgb565_swap(px_map,size);
    esp_lcd_panel_draw_bitmap(panel,area->x1,area->y1,
        area->x2+1,area->y2+1,px_map);
}

static void lvgl_tick_cb(void *arg){(void)arg;lv_tick_inc(LVGL_TICK_PERIOD_MS);}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (true) {
        if (lvgl_lock()) {
            uint32_t wait=lv_timer_handler();lvgl_unlock();
            if (wait<5) wait=5; else if (wait>100) wait=100;
            vTaskDelay(pdMS_TO_TICKS(wait));
        } else vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void game_timer_cb(lv_timer_t *timer){(void)timer;update_game();}

static void init_backlight(void)
{
    const ledc_timer_config_t timer={
        .speed_mode=LEDC_LOW_SPEED_MODE,.timer_num=LEDC_TIMER_0,
        .duty_resolution=LEDC_TIMER_8_BIT,.freq_hz=LCD_BACKLIGHT_PWM_HZ,
        .clk_cfg=LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const ledc_channel_config_t channel={
        .gpio_num=PIN_LCD_BL,.speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0,.timer_sel=LEDC_TIMER_0,
        .duty=LCD_BACKLIGHT_DEFAULT,.hpoint=0};
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static esp_err_t init_display(void)
{
    init_backlight();
    const spi_bus_config_t bus={
        .sclk_io_num=PIN_LCD_SCK,.mosi_io_num=PIN_LCD_MOSI,.miso_io_num=-1,
        .quadwp_io_num=-1,.quadhd_io_num=-1,
        .max_transfer_sz=SCREEN_W*LVGL_DRAW_BUF_LINES*sizeof(lv_color_t)};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST,&bus,SPI_DMA_CH_AUTO),TAG,"spi");
    esp_lcd_panel_io_handle_t io=NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg={
        .dc_gpio_num=PIN_LCD_DC,.cs_gpio_num=PIN_LCD_CS,
        .pclk_hz=LCD_PIXEL_CLOCK_HZ,.lcd_cmd_bits=8,.lcd_param_bits=8,
        .spi_mode=0,.trans_queue_depth=10};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST,&io_cfg,&io),TAG,"panel io");
    const esp_lcd_panel_dev_config_t panel_cfg={
        .reset_gpio_num=PIN_LCD_RST,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel=16};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io,&panel_cfg,&s_panel),TAG,"panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),TAG,"reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),TAG,"init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel,true),TAG,"invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel,false),TAG,"swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel,false,false),TAG,"mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel,LCD_X_GAP,LCD_Y_GAP),TAG,"gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel,true),TAG,"on");
    lv_init();
    s_display=lv_display_create(SCREEN_W,SCREEN_H);
    lv_display_set_user_data(s_display,s_panel);
    lv_display_set_flush_cb(s_display,lvgl_flush_cb);
    size_t draw_size=SCREEN_W*LVGL_DRAW_BUF_LINES*sizeof(lv_color_t);
    void *draw=heap_caps_malloc(draw_size,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(draw,ESP_ERR_NO_MEM,TAG,"draw buffer");
    lv_display_set_buffers(s_display,draw,NULL,draw_size,LV_DISPLAY_RENDER_MODE_PARTIAL);
    const esp_lcd_panel_io_callbacks_t callbacks={.on_color_trans_done=notify_flush};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(
        io,&callbacks,s_display),TAG,"callbacks");
    const esp_timer_create_args_t tick={.callback=lvgl_tick_cb,.name="lvgl_tick"};
    esp_timer_handle_t timer=NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick,&timer),TAG,"tick");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(
        timer,LVGL_TICK_PERIOD_MS*1000),TAG,"tick start");
    xTaskCreate(lvgl_task,"lvgl",4096,NULL,3,NULL);
    ESP_LOGI(TAG,"display portrait %dx%d",SCREEN_W,SCREEN_H);
    return ESP_OK;
}

static void create_ui(void)
{
    lv_obj_t *screen=lv_display_get_screen_active(s_display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x090705),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    s_canvas_buffer=heap_caps_calloc(SCREEN_W*SCREEN_H,sizeof(lv_color16_t),
                                     MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!s_canvas_buffer) s_canvas_buffer=heap_caps_calloc(
        SCREEN_W*SCREEN_H,sizeof(lv_color16_t),MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(s_canvas_buffer?ESP_OK:ESP_ERR_NO_MEM);
    s_canvas=lv_canvas_create(screen);
    lv_canvas_set_buffer(s_canvas,s_canvas_buffer,SCREEN_W,SCREEN_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas,0,0);
    render();
    lv_timer_create(game_timer_cb,GAME_TIMER_MS,NULL);
}

static void app_task(void *arg)
{
    (void)arg;game_event_t event;
    while (true) if (xQueueReceive(s_events,&event,portMAX_DELAY)==pdTRUE)
        handle_event(event);
}

static void persistence_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(250));
        int64_t now=now_ms();
        if (!s_stats_dirty || s_stats_flush_ms==0 ||
            now<s_stats_flush_ms) continue;
        saved_stats_t snapshot;
        if (!lvgl_lock()) continue;
        snapshot=s_stats;
        snapshot.free_spins=s_free_spins;
        snapshot.big_bang_armed=s_big_bang_armed?1:0;
        memcpy(snapshot.bets,s_bets,sizeof(snapshot.bets));
        memcpy(snapshot.prepaid_bets,s_prepaid_bets,
               sizeof(snapshot.prepaid_bets));
        s_stats_dirty=false;
        s_stats_flush_ms=0;
        lvgl_unlock();
        esp_err_t err=write_stats_snapshot(&snapshot);
        if (err!=ESP_OK) {
            ESP_LOGW(TAG,"NVS save deferred: %s",esp_err_to_name(err));
            s_stats_dirty=true;
            s_stats_flush_ms=now_ms()+fruit_game_tuning.nvs_flush_delay_ms;
        }
    }
}

static void battery_task(void *arg)
{
    (void)arg;
    while (true) {
        int level=-1;
        bool charging=false;
        bool usb_powered=false;
        bool level_ok=vibe_board_battery_level(&level)==ESP_OK;
        bool charging_ok=vibe_board_battery_charging(&charging)==ESP_OK;
        bool usb_ok=vibe_board_usb_powered(&usb_powered)==ESP_OK;
        if (lvgl_lock()) {
            bool changed=(level_ok && level!=s_battery_level) ||
                (charging_ok && charging!=s_battery_charging) ||
                (usb_ok && usb_powered!=s_usb_powered);
            if (level_ok) s_battery_level=level;
            if (charging_ok) s_battery_charging=charging;
            if (usb_ok) s_usb_powered=usb_powered;
            if (changed) {
                ESP_LOGI(TAG,"battery status level=%d charging=%d usb=%d",
                         s_battery_level,s_battery_charging,s_usb_powered);
                render();
            }
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(BATTERY_POLL_MS));
    }
}

static void motion_task(void *arg)
{
    (void)arg;
    int16_t x=0, y=0, z=0;
    int32_t baseline_y=0, baseline_z=0;
    bool calibrated=false;
    bool armed=true;
    uint8_t neutral_samples=0;
    uint8_t candidate_samples=0;
    uint8_t drift_samples=0;
    game_event_t candidate_event=EVENT_ACTIVATE;
    int64_t cooldown_until=0;
    int64_t disarmed_since=0;
    bool have_previous_sample=false;
    int16_t previous_x=0,previous_y=0,previous_z=0;
    uint8_t stable_samples=0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(fruit_game_tuning.motion_sample_interval_ms));
        if (vibe_board_accel_read(&x,&y,&z)!=ESP_OK) continue;
        int32_t step_x=have_previous_sample?(int32_t)x-previous_x:0;
        int32_t step_y=have_previous_sample?(int32_t)y-previous_y:0;
        int32_t step_z=have_previous_sample?(int32_t)z-previous_z:0;
        if (step_x<0) step_x=-step_x;
        if (step_y<0) step_y=-step_y;
        if (step_z<0) step_z=-step_z;
        previous_x=x;
        previous_y=y;
        previous_z=z;
        have_previous_sample=true;
        if (!calibrated) {
            baseline_y=y;
            baseline_z=z;
            calibrated=true;
            continue;
        }

        int32_t motion_y=(int32_t)y-baseline_y;
        int32_t motion_z=(int32_t)z-baseline_z;
        int32_t abs_y=motion_y<0?-motion_y:motion_y;
        int32_t abs_z=motion_z<0?-motion_z:motion_z;
        int64_t now=now_ms();
        bool sample_stable=
            step_x<fruit_game_tuning.motion_stable_raw &&
            step_y<fruit_game_tuning.motion_stable_raw &&
            step_z<fruit_game_tuning.motion_stable_raw;

        if (!armed) {
            if (abs_y<fruit_game_tuning.motion_release_raw &&
                abs_z<fruit_game_tuning.motion_release_raw) {
                if (neutral_samples<UINT8_MAX) ++neutral_samples;
            } else neutral_samples=0;
            if (now>=cooldown_until &&
                neutral_samples>=fruit_game_tuning.motion_rearm_samples) {
                armed=true;
                neutral_samples=0;
                candidate_samples=0;
                drift_samples=0;
                baseline_y=y;
                baseline_z=z;
                stable_samples=0;
                ESP_LOGI(TAG,"motion rearmed after physical center");
                continue;
            }

            int64_t disarmed_ms=now-disarmed_since;
            if (disarmed_ms>=fruit_game_tuning.motion_park_timeout_ms) {
                if (sample_stable) {
                    if (stable_samples<UINT8_MAX) ++stable_samples;
                } else stable_samples=0;
                bool stable_recovery=
                    stable_samples>=fruit_game_tuning.motion_park_samples;
                bool hard_recovery=
                    disarmed_ms>=
                    (int64_t)fruit_game_tuning.motion_park_timeout_ms*4;
                if (stable_recovery || hard_recovery) {
                    baseline_y=y;
                    baseline_z=z;
                    armed=true;
                    neutral_samples=0;
                    candidate_samples=0;
                    drift_samples=0;
                    stable_samples=0;
                    ESP_LOGI(TAG,
                             "motion auto-rearmed reason=%s "
                             "delta_y=%ld delta_z=%ld disarmed_ms=%lld",
                             stable_recovery?"stable":"timeout",
                             (long)motion_y,(long)motion_z,
                             (long long)disarmed_ms);
                }
            }
            continue;
        }

        /*
         * A slow change in the user's neutral holding angle can leave the
         * baseline frozen in the release/trigger hysteresis band.  Distinguish
         * that from a deliberate gesture by requiring a long run of tiny
         * sample-to-sample changes before adopting the new stable pose.
         * Normal direction gestures cross the threshold long before this
         * recovery window expires.
         */
        if (abs_y>=fruit_game_tuning.motion_release_raw ||
            abs_z>=fruit_game_tuning.motion_release_raw) {
            if (sample_stable) {
                if (drift_samples<UINT8_MAX) ++drift_samples;
            } else drift_samples=0;
            if (drift_samples>=
                fruit_game_tuning.motion_pickup_settle_samples) {
                ESP_LOGI(TAG,
                         "motion baseline recovered after stable drift "
                         "delta_y=%ld delta_z=%ld",
                         (long)motion_y,(long)motion_z);
                baseline_y=y;
                baseline_z=z;
                candidate_samples=0;
                drift_samples=0;
                continue;
            }
        } else drift_samples=0;

        if (abs_y<fruit_game_tuning.motion_trigger_raw &&
            abs_z<fruit_game_tuning.motion_trigger_raw) {
            candidate_samples=0;
            /*
             * Track slow neutral-pose drift only while armed and centered.
             * Once a direction fires, the baseline stays frozen until the
             * device physically returns to the release zone.
             */
            if (abs_y<fruit_game_tuning.motion_release_raw &&
                abs_z<fruit_game_tuning.motion_release_raw) {
                baseline_y+=motion_y/16;
                baseline_z+=motion_z/16;
            }
            continue;
        }

        game_event_t event;
        const char *direction;
        bool y_dominant=
            abs_y*100>=abs_z*fruit_game_tuning.motion_axis_dominance_percent;
        bool z_dominant=
            abs_z*100>=abs_y*fruit_game_tuning.motion_axis_dominance_percent;
        if (y_dominant) {
            event=motion_y>0?EVENT_MOTION_LEFT:EVENT_MOTION_RIGHT;
            direction=motion_y>0?"left":"right";
        } else if (z_dominant) {
            event=motion_z>0?EVENT_MOTION_UP:EVENT_MOTION_DOWN;
            direction=motion_z>0?"up":"down";
        } else {
            candidate_samples=0;
            continue;
        }
        if (candidate_samples==0 || candidate_event!=event) {
            candidate_event=event;
            candidate_samples=1;
            continue;
        }
        if (candidate_samples<UINT8_MAX) ++candidate_samples;
        if (candidate_samples<fruit_game_tuning.motion_confirm_samples) {
            continue;
        }
        ESP_LOGI(TAG,"motion direction=%s delta_y=%ld delta_z=%ld confirmed=%u",
                 direction,(long)motion_y,(long)motion_z,
                 (unsigned)candidate_samples);
        queue_event(event);
        armed=false;
        neutral_samples=0;
        candidate_samples=0;
        drift_samples=0;
        disarmed_since=now;
        stable_samples=0;
        cooldown_until=now+fruit_game_tuning.motion_cooldown_ms;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "boot VibeStick Fruit Machine 0.7.0 lucky-protection");
    esp_err_t result=nvs_flash_init();
    if (result==ESP_ERR_NVS_NO_FREE_PAGES ||
        result==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());ESP_ERROR_CHECK(nvs_flash_init());
    } else ESP_ERROR_CHECK(result);
    load_stats();
    ESP_ERROR_CHECK(run_logic_self_test());
    ESP_ERROR_CHECK_WITHOUT_ABORT(vibe_board_init_power());
    ESP_ERROR_CHECK_WITHOUT_ABORT(vibe_board_battery_level(&s_battery_level));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        vibe_board_battery_charging(&s_battery_charging));
    ESP_ERROR_CHECK_WITHOUT_ABORT(vibe_board_usb_powered(&s_usb_powered));
    ESP_LOGI(TAG,"battery initial level=%d charging=%d usb=%d",
             s_battery_level,s_battery_charging,s_usb_powered);
    esp_err_t imu_status=vibe_board_imu_init();
    if (imu_status!=ESP_OK) {
        ESP_LOGW(TAG,"motion navigation disabled: %s",esp_err_to_name(imu_status));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(fruit_audio_init());
    s_events=xQueueCreate(12,sizeof(game_event_t));
    s_lvgl_lock=xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(init_display());
    if (lvgl_lock()) {create_ui();lvgl_unlock();}
    ESP_ERROR_CHECK(init_buttons());
    xTaskCreate(app_task,"fruit_game",4096,NULL,4,NULL);
    xTaskCreate(persistence_task,"fruit_save",3072,NULL,2,NULL);
    xTaskCreate(battery_task,"fruit_battery",3072,NULL,2,NULL);
    if (imu_status==ESP_OK) {
        xTaskCreate(motion_task,"fruit_motion",3072,NULL,3,NULL);
    }
    if (s_free_spins>0 && total_bet()>0) s_auto_spin_ms=now_ms()+1000;
    ESP_LOGI(TAG,
        "ready credit=%ld high=%ld rounds=%lu wins=%lu best=%ld gem=%u gem_credit=%ld controls=motion-four-way side-short-topup side-long-gallery front-activate",
        (long)s_stats.credit,(long)s_stats.high_credit,
        (unsigned long)s_stats.total_rounds,(unsigned long)s_stats.winning_rounds,
        (long)s_stats.highest_single_win,
        s_stats.gem_level,(long)s_stats.credit);
}
