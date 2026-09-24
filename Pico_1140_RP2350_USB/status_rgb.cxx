#include "status_rgb.h"

#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

namespace {

constexpr uint led_pin = 22;
constexpr uint32_t led_frequency = 800000;
constexpr uint64_t activity_time_us = 50000;

constexpr uint32_t off = 0x000000;
constexpr uint32_t boot = 0x001818;
constexpr uint32_t initializing = 0x180600;
constexpr uint32_t running = 0x001000;
constexpr uint32_t reading = 0x000018;
constexpr uint32_t writing = 0x120012;
constexpr uint32_t error = 0x180000;

PIO led_pio = pio0;
uint led_sm;
uint led_offset;
uint32_t current_color = off;
uint64_t activity_until;
bool initialized;
bool activity_active;

void set_color(uint32_t rgb) {
    if (!initialized || rgb == current_color)
        return;

    const uint32_t red = (rgb >> 16) & 0xff;
    const uint32_t green = (rgb >> 8) & 0xff;
    const uint32_t blue = rgb & 0xff;
    const uint32_t grb = (green << 16) | (red << 8) | blue;
    pio_sm_put_blocking(led_pio, led_sm, grb << 8);
    current_color = rgb;
}

} // namespace

void status_rgb_init() {
    led_sm = pio_claim_unused_sm(led_pio, true);
    led_offset = pio_add_program(led_pio, &ws2812_program);
    ws2812_program_init(led_pio, led_sm, led_offset, led_pin, led_frequency,
                        false);
    initialized = true;
    current_color = 0xffffffff;
    set_color(off);
    sleep_us(80); // WS2812 reset/latch interval before the first visible color.
}

void status_rgb_boot_animation() {
    for (unsigned i = 0; i < 3; ++i) {
        set_color(boot);
        sleep_ms(90);
        set_color(off);
        sleep_ms(70);
    }
    set_color(initializing);
}

void status_rgb_initializing() {
    activity_active = false;
    set_color(initializing);
}

void status_rgb_running() {
    activity_active = false;
    set_color(running);
}

void status_rgb_disk_activity(bool write) {
    activity_until = time_us_64() + activity_time_us;
    activity_active = true;
    set_color(write ? writing : reading);
}

void status_rgb_poll(uint64_t now_us) {
    if (activity_active && now_us >= activity_until)
        status_rgb_running();
}

void status_rgb_error() {
    activity_active = false;
    set_color(error);
}
