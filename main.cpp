#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include <cstdio>
#include <iterator>
#include <sstream>
#include <algorithm>

#include "drivers/dv_display/dv_display.hpp"
#include "libraries/pico_graphics/pico_graphics_dv.hpp"

#include "fixed_fft.hpp"

extern "C" {
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"

#include "music_file.h"

#include "picow_bt_example_common.h"
}

FATFS fs;
music_file mf;

// Working buffer for reading from file
#define MP3_CACHE_BUFFER 8192
static unsigned char mp3_cache_buffer[MP3_CACHE_BUFFER];

static sd_sdio_if_t sdio_if = {
    /*
    Pins CLK_gpio, D1_gpio, D2_gpio, and D3_gpio are at offsets from pin D0_gpio.
    The offsets are determined by sd_driver\SDIO\rp2040_sdio.pio.
        CLK_gpio = (D0_gpio + SDIO_CLK_PIN_D0_OFFSET) % 32;
        As of this writing, SDIO_CLK_PIN_D0_OFFSET is 30,
            which is -2 in mod32 arithmetic, so:
        CLK_gpio = D0_gpio -2.
        D1_gpio = D0_gpio + 1;
        D2_gpio = D0_gpio + 2;
        D3_gpio = D0_gpio + 3;
    */
    .CMD_gpio = SDCARD_PIN_SPI0_MOSI,
    .D0_gpio = SDCARD_PIN_SPI0_MISO,
    .baud_rate = 266 * 1000 * 1000 / 6
};

/* Hardware Configuration of the SD Card socket "object" */
static sd_card_t sd_card = {.type = SD_IF_SDIO, .sdio_if_p = &sdio_if};

/**
 * @brief Get the number of SD cards.
 *
 * @return The number of SD cards, which is 1 in this case.
 */
size_t sd_get_num() { return 1; }

/**
 * @brief Get a pointer to an SD card object by its number.
 *
 * @param[in] num The number of the SD card to get.
 *
 * @return A pointer to the SD card object, or @c NULL if the number is invalid.
 */
sd_card_t* sd_get_by_num(size_t num) {
    if (0 == num) {
        // The number 0 is a valid SD card number.
        // Return a pointer to the sd_card object.
        return &sd_card;
    } else {
        // The number is invalid. Return @c NULL.
        return NULL;
    }
}

extern "C" void get_audio(int16_t * pcm_buffer, int num_samples_to_write);

volatile bool got_audio;
volatile bool audio_eof;

static FIX_FFT fft;
volatile bool need_update;

#define AUDIO_BUFFER_LEN 5000
int16_t audio_buffer1[AUDIO_BUFFER_LEN];
int16_t audio_buffer2[AUDIO_BUFFER_LEN];
int16_t* volatile audio_buffer = audio_buffer1;
int16_t* volatile audio_buffer_next = audio_buffer2;
uint32_t audio_valid;
volatile uint32_t audio_valid_next;
uint32_t audio_read_idx;

int copy_from_audio_buffer(int16_t * pcm_buffer, int max_samples) {
    //printf("Have %d samples, need %d\n", audio_valid - audio_read_idx, max_samples);
    int samples = std::min(int(audio_valid - audio_read_idx), max_samples);
    memcpy(pcm_buffer, &audio_buffer[audio_read_idx], 2 * samples);
    audio_read_idx += samples;
    return samples;
}

void get_audio(int16_t * pcm_buffer, int num_samples_to_write) {
    num_samples_to_write <<= 1; // Stereo samples

    if (!need_update &&
        (audio_read_idx & 0x7ff) == 0 && (audio_valid - audio_read_idx) >= 2048) {
        fft.fill(&audio_buffer[audio_read_idx], 2);
        need_update = true;
    }

    if (audio_valid - audio_read_idx) {
        int samples = copy_from_audio_buffer(pcm_buffer, num_samples_to_write);
        num_samples_to_write -= samples;
        pcm_buffer += samples;
    }

    if (num_samples_to_write && !audio_valid_next) {
        sleep_ms(5);
    }

    if (num_samples_to_write && audio_valid_next) {
        audio_valid = audio_valid_next;
        std::swap(audio_buffer, audio_buffer_next);
        audio_valid_next = 0;
        __sev();
        audio_read_idx = 0;

        int samples = copy_from_audio_buffer(pcm_buffer, num_samples_to_write);
        num_samples_to_write -= samples;
        pcm_buffer += samples;
    }

    if (num_samples_to_write) {
        printf("Didn't have %d samples\n", num_samples_to_write);
        memset(pcm_buffer, 0, num_samples_to_write * 2);
    }
    else {
        got_audio = true;
    }
}

#define MAX_FNAME_LEN 80
#define MAX_FILENAMES 128
static char all_filenames[MAX_FNAME_LEN * MAX_FILENAMES];
static int16_t filename_idx[MAX_FILENAMES];
const char* playing_file;

void core1_main() {
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
      printf("Failed to mount SD card, error: %d\n", fr);
      return;
    }

    printf("SD card mounted!\n");

    DIR dj = {};      /* Directory object */
    FILINFO fno = {}; /* File information */
    fr = f_findfirst(&dj, &fno, "", "*.mp3");
    if (FR_OK != fr) {
        printf("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }

    int idx = 0;
    char* fname_ptr = all_filenames;

    while (fr == FR_OK && fno.fname[0] && idx < MAX_FILENAMES) { /* Repeat while an item is found */
        printf("Found: %s\n", fno.fname);
        filename_idx[idx++] = fname_ptr - all_filenames;
        fname_ptr = stpcpy(fname_ptr, fno.fname) + 1;

        fr = f_findnext(&dj, &fno); /* Search for next item */
    }
    f_closedir(&dj);

    std::sort(filename_idx, &filename_idx[idx], [](int16_t a, int16_t b) {
        return strcmp(&all_filenames[a], &all_filenames[b]) < 0;
    });

    int num_files = idx;
    bool first = true;

    while (1) {
        for (idx = 0; idx < num_files; ++idx) {
            playing_file = &all_filenames[filename_idx[idx]];
            printf("Playing: %s\n", playing_file);
            if (!musicFileCreate(&mf, playing_file, mp3_cache_buffer, MP3_CACHE_BUFFER))
            {
                printf("Cannot open mp3 file\n");
                return;
            }

            if (first) {
                musicFileRead(&mf, audio_buffer, AUDIO_BUFFER_LEN, &audio_valid);
                first = false;
            }

            while (1) {
                if (!audio_valid_next) {
                    uint32_t samples_read;
                    bool ok = musicFileRead(&mf, audio_buffer_next, AUDIO_BUFFER_LEN, &samples_read);
                    if (!ok) break;
                    audio_valid_next = samples_read;
                }
                //__wfe();
            }

            musicFileClose(&mf);
        }
    }
}

#define FRAME_WIDTH 720
#define FRAME_HEIGHT 480

#define NUM_SAMPLES 1024
#define DISPLAY_SAMPLES 57
#define SAMPLE_WIDTH 8
#define DISPLAY_WIDTH (DISPLAY_SAMPLES * SAMPLE_WIDTH)
#define SAMPLE_HEIGHT 320

#define SAMPLE_X 132
#define SAMPLE_Y 90

static int display_samples[DISPLAY_SAMPLES];

using namespace pimoroni;

DVDisplay display;
PicoGraphics_PenDV_RGB555 graphics(FRAME_WIDTH, FRAME_HEIGHT, display);

Pen BLACK = graphics.create_pen(0, 0, 0);
Pen WHITE = graphics.create_pen(255, 255, 255);

int main() {
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(266000, true);
    stdio_init_all();

    sleep_ms(5000);
    printf("Hello\n");

    fft.set_scale(.2f);

    printf("Init Video...\n");
    display.preinit();
    display.init(FRAME_WIDTH, FRAME_HEIGHT, DVDisplay::MODE_RGB555);
    printf("Done!\n");

    multicore_launch_core1(core1_main);

    int res = picow_bt_example_init();
    if (res){
        return -1;
    }

    picow_bt_example_main();

    graphics.set_font("bitmap8");

    while(true) {
        graphics.set_pen(BLACK);
        graphics.clear();
        graphics.set_pen(WHITE);
        graphics.text("PicoVision MP3 Player!", Point(10, 10), FRAME_WIDTH-20);
        graphics.text("Now Playing: ", Point(10, 40), FRAME_WIDTH-20);
        if (playing_file) {
            graphics.text(playing_file, Point(130, 40), FRAME_WIDTH-140);
        }
        if (need_update) {
            fft.update();
            for (int i = 0; i < DISPLAY_SAMPLES; ++i) {
                display_samples[i] = (display_samples[i] + fft.get_scaled(i)) >> 1;
            }
        }
        need_update = false;
        const int max_y = SAMPLE_Y + SAMPLE_HEIGHT;
        for (int i = 0, x = SAMPLE_X; i < DISPLAY_SAMPLES; ++i, x += SAMPLE_WIDTH) {
            for (int y = std::min(display_samples[i] - 1, max_y); y >= 0; --y) {
                graphics.set_pen(0, 0, 80 + (y >> 1));
                graphics.set_pixel_span(Point(x, max_y - y), SAMPLE_WIDTH);
            }
        }        
        display.flip();
    }
}