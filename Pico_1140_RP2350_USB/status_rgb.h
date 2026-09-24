#pragma once

#include <cstdint>

void status_rgb_init();
void status_rgb_boot_animation();
void status_rgb_initializing();
void status_rgb_running();
void status_rgb_disk_activity(bool write);
void status_rgb_poll(uint64_t now_us);
void status_rgb_error();
