#include "fruit_audio.h"

#include <math.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "vibe_board.h"

#define PIN_ES8311_MCLK 18
#define PIN_ES8311_BCLK 17
#define PIN_ES8311_LRCK 15
#define PIN_ES8311_DIN 14
#define SAMPLE_RATE 16000
#define FRAME_SAMPLES 160
#define TWO_PI 6.28318530717958647692f

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
} note_t;

static const char *TAG = "fruit_audio";
static QueueHandle_t s_sound_queue;
static i2s_chan_handle_t s_tx;
static esp_codec_dev_handle_t s_codec;
static const audio_codec_ctrl_if_t *s_ctrl;
static const audio_codec_data_if_t *s_data;
static const audio_codec_gpio_if_t *s_gpio;
static const audio_codec_if_t *s_codec_if;

static void close_audio(void)
{
    if (s_codec) {
        esp_codec_dev_close(s_codec);
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
    }
    if (s_codec_if) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
    }
    if (s_data) {
        audio_codec_delete_data_if(s_data);
        s_data = NULL;
    }
    if (s_gpio) {
        audio_codec_delete_gpio_if(s_gpio);
        s_gpio = NULL;
    }
    if (s_ctrl) {
        audio_codec_delete_ctrl_if(s_ctrl);
        s_ctrl = NULL;
    }
    if (s_tx) {
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(vibe_board_speaker_set_enabled(false));
}

static esp_err_t open_audio(void)
{
    ESP_RETURN_ON_ERROR(vibe_board_speaker_set_enabled(true), TAG, "speaker");
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &s_tx, NULL), TAG, "i2s");

    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_ES8311_MCLK,
            .bclk = PIN_ES8311_BCLK,
            .ws = PIN_ES8311_LRCK,
            .dout = PIN_ES8311_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &config), TAG, "i2s mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s enable");

    audio_codec_i2c_cfg_t i2c = {
        .port = I2C_NUM_1,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = vibe_board_i2c_bus(),
    };
    s_ctrl = audio_codec_new_i2c_ctrl(&i2c);
    audio_codec_i2s_cfg_t data = {.port = I2S_NUM_1, .tx_handle = s_tx};
    s_data = audio_codec_new_i2s_data(&data);
    s_gpio = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(s_ctrl && s_data && s_gpio, ESP_ERR_NO_MEM, TAG, "codec if");

    es8311_codec_cfg_t codec = {
        .ctrl_if = s_ctrl,
        .gpio_if = s_gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .master_mode = false,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    s_codec_if = es8311_codec_new(&codec);
    ESP_RETURN_ON_FALSE(s_codec_if, ESP_ERR_NO_MEM, TAG, "codec");
    esp_codec_dev_cfg_t device = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data,
    };
    s_codec = esp_codec_dev_new(&device);
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_NO_MEM, TAG, "codec dev");
    esp_codec_dev_sample_info_t sample = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 1,
        .channel_mask = I2S_STD_SLOT_LEFT,
        .sample_rate = SAMPLE_RATE,
    };
    ESP_RETURN_ON_FALSE(
        esp_codec_dev_open(s_codec, &sample) == ESP_CODEC_DEV_OK,
        ESP_FAIL, TAG, "codec open");
    ESP_RETURN_ON_FALSE(
        esp_codec_dev_set_out_vol(s_codec, 64) == ESP_CODEC_DEV_OK,
        ESP_FAIL, TAG, "volume");
    ESP_RETURN_ON_FALSE(
        esp_codec_dev_set_out_mute(s_codec, false) == ESP_CODEC_DEV_OK,
        ESP_FAIL, TAG, "unmute");
    return ESP_OK;
}

static esp_err_t play_note(note_t note)
{
    int count = SAMPLE_RATE * note.duration_ms / 1000;
    int16_t frame[FRAME_SAMPLES];
    for (int written = 0; written < count;) {
        int n = count - written;
        if (n > FRAME_SAMPLES) {
            n = FRAME_SAMPLES;
        }
        for (int i = 0; i < n; ++i) {
            int sample = written + i;
            float edge = 1.0f;
            if (sample < 100) {
                edge = sample / 100.0f;
            } else if (count - sample < 100) {
                edge = (count - sample) / 100.0f;
            }
            float phase = TWO_PI * note.frequency_hz * sample / SAMPLE_RATE;
            frame[i] = note.frequency_hz == 0
                ? 0
                : (int16_t)(sinf(phase) * edge * 0.12f * 32767.0f);
        }
        ESP_RETURN_ON_FALSE(
            esp_codec_dev_write(s_codec, frame, n * sizeof(int16_t)) ==
                ESP_CODEC_DEV_OK,
            ESP_FAIL, TAG, "audio write");
        written += n;
    }
    return ESP_OK;
}

static esp_err_t play_bell_tick(void)
{
    const int count = SAMPLE_RATE * 24 / 1000;
    int16_t frame[FRAME_SAMPLES];
    for (int written = 0; written < count;) {
        int n = count - written;
        if (n > FRAME_SAMPLES) {
            n = FRAME_SAMPLES;
        }
        for (int i = 0; i < n; ++i) {
            int sample = written + i;
            float position = sample / (float)count;
            float attack = sample < 32 ? sample / 32.0f : 1.0f;
            float decay = 1.0f - position;
            float envelope = attack * decay * decay;
            float time = sample / (float)SAMPLE_RATE;
            float tone =
                sinf(TWO_PI * 1760.0f * time) * 0.10f +
                sinf(TWO_PI * 2640.0f * time) * 0.055f +
                sinf(TWO_PI * 3520.0f * time) * 0.03f;
            frame[i] = (int16_t)(tone * envelope * 32767.0f);
        }
        ESP_RETURN_ON_FALSE(
            esp_codec_dev_write(s_codec, frame, n * sizeof(int16_t)) ==
                ESP_CODEC_DEV_OK,
            ESP_FAIL, TAG, "bell tick write");
        written += n;
    }
    return ESP_OK;
}

static esp_err_t play_sound_sequence(fruit_sound_t sound)
{
    static const note_t start[] = {{440, 55}, {660, 70}};
    static const note_t win[] = {{523, 90}, {659, 90}, {784, 100}, {1047, 180}};
    /*
     * Every stop result starts with the same short, bright "landing" ping.
     * Higher payout tiers then add a progressively taller and faster flourish.
     */
    static const note_t win_low[] = {
        {2093, 42}, {0, 18}, {1047, 58}, {1319, 90}
    };
    static const note_t win_medium[] = {
        {2093, 42}, {0, 16}, {1047, 55}, {1319, 55}, {1661, 110}
    };
    static const note_t win_high[] = {
        {2349, 42}, {0, 14}, {1175, 48}, {1568, 48},
        {1976, 55}, {2637, 130}
    };
    static const note_t win_top[] = {
        {2637, 45}, {0, 12},
        {1319, 42}, {1661, 42}, {2093, 48}, {2637, 60},
        {0, 22},
        {2093, 42}, {2637, 42}, {3136, 170}
    };
    static const note_t bonus[] = {
        {784, 70}, {988, 70}, {1175, 70}, {1568, 190}, {1175, 80}, {1568, 210}
    };
    static const note_t lose[] = {
        {2093, 42}, {0, 20}, {392, 65}, {330, 90}
    };
    static const note_t jackpot[] = {
        {523, 65}, {659, 65}, {784, 65}, {1047, 130},
        {0, 45},
        {784, 55}, {988, 55}, {1175, 55}, {1568, 145},
        {0, 35},
        {1319, 70}, {1568, 70}, {2093, 260}
    };
    static const note_t big_bang[] = {
        {147, 60}, {196, 60}, {294, 60}, {440, 70},
        {659, 75}, {988, 90}, {1319, 220}
    };
    if (sound == FRUIT_SOUND_TICK) {
        return play_bell_tick();
    }
    const note_t *notes = start;
    size_t length = sizeof(start) / sizeof(start[0]);
    if (sound == FRUIT_SOUND_START) {
        notes = start;
        length = sizeof(start) / sizeof(start[0]);
    } else if (sound == FRUIT_SOUND_WIN) {
        notes = win;
        length = sizeof(win) / sizeof(win[0]);
    } else if (sound == FRUIT_SOUND_WIN_LOW) {
        notes = win_low;
        length = sizeof(win_low) / sizeof(win_low[0]);
    } else if (sound == FRUIT_SOUND_WIN_MEDIUM) {
        notes = win_medium;
        length = sizeof(win_medium) / sizeof(win_medium[0]);
    } else if (sound == FRUIT_SOUND_WIN_HIGH) {
        notes = win_high;
        length = sizeof(win_high) / sizeof(win_high[0]);
    } else if (sound == FRUIT_SOUND_WIN_TOP) {
        notes = win_top;
        length = sizeof(win_top) / sizeof(win_top[0]);
    } else if (sound == FRUIT_SOUND_BONUS) {
        notes = bonus;
        length = sizeof(bonus) / sizeof(bonus[0]);
    } else if (sound == FRUIT_SOUND_LOSE) {
        notes = lose;
        length = sizeof(lose) / sizeof(lose[0]);
    } else if (sound == FRUIT_SOUND_JACKPOT) {
        notes = jackpot;
        length = sizeof(jackpot) / sizeof(jackpot[0]);
    } else if (sound == FRUIT_SOUND_BIG_BANG) {
        notes = big_bang;
        length = sizeof(big_bang) / sizeof(big_bang[0]);
    }
    esp_err_t err=ESP_OK;
    for (size_t i = 0; i < length && err == ESP_OK; ++i) {
        err = play_note(notes[i]);
    }
    return err;
}

static void sound_task(void *argument)
{
    (void)argument;
    esp_err_t audio_status=open_audio();
    if (audio_status!=ESP_OK) {
        ESP_LOGW(TAG,"initial audio open failed: %s",esp_err_to_name(audio_status));
        close_audio();
    }
    fruit_sound_t sound;
    while (true) {
        if (xQueueReceive(s_sound_queue,&sound,portMAX_DELAY)!=pdTRUE) continue;
        if (audio_status!=ESP_OK) {
            close_audio();
            audio_status=open_audio();
            if (audio_status!=ESP_OK) close_audio();
        }
        if (audio_status==ESP_OK) {
            audio_status=play_sound_sequence(sound);
            if (audio_status!=ESP_OK) close_audio();
        }
    }
}

esp_err_t fruit_audio_init(void)
{
    if (s_sound_queue) return ESP_OK;
    s_sound_queue=xQueueCreate(16,sizeof(fruit_sound_t));
    if (!s_sound_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sound_task,"fruit_audio",6144,NULL,4,NULL)!=pdPASS) {
        vQueueDelete(s_sound_queue);
        s_sound_queue=NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t fruit_audio_play(fruit_sound_t sound)
{
    if (!s_sound_queue) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_sound_queue,&sound,0)!=pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}
