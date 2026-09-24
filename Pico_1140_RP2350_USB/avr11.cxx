#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <cstdlib>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avr11.h"
#include "kb11.h"
#include "pico/stdlib.h"
#include "status_rgb.h"
#include "tusb.h"

KB11 cpu;
int kbdelay = 0;
int clkdelay = 0;
uint64_t systime,nowtime,clkdiv;
void setup()
 {
    status_rgb_initializing();
    printf("Initializing internal flash RK05...\r\n");
    if (!cpu.unibus.rk11.rk05.init()) {
        status_rgb_error();
        panic("Internal flash disk initialization failed");
    }
    printf("RK05 ready: %lu bytes\r\n",
           static_cast<unsigned long>(cpu.unibus.rk11.rk05.size()));
    clkdiv = (uint64_t)1000000 / (uint64_t)60;
    systime = time_us_64();
    cpu.reset(02002,0);
    status_rgb_running();
    printf("Ready\n");
}

jmp_buf trapbuf;

void loop0();

[[noreturn]] void trap(uint16_t vec) { longjmp(trapbuf, vec); }

void loop() {
    auto vec = setjmp(trapbuf);
    if (vec == 0) {
        loop0();
    } else {
        cpu.trapat(vec);
    }
}

void loop0() {
    static uint32_t stepcnt = 0;

    while (true) {
        stepcnt++;

        if ((cpu.itab[0].vec > 0) && (cpu.itab[0].pri > cpu.priority())) {
            cpu.trapat(cpu.itab[0].vec);
            cpu.popirq();
            return; // exit from loop to reset trapbuf
        }

        if (!cpu.wtstate)
           cpu.step();

        cpu.unibus.rk11.step();
        cpu.unibus.rl11.step();

        if (kbdelay++ == 50000) {  // Poll less frequently to avoid blocking
            tud_task();  // CRITICAL: Process USB BEFORE polling
            cpu.unibus.cons.poll();
            // Note: dl11.poll() causes blocking - leave disabled
            // cpu.unibus.dl11.poll();
            kbdelay = 0;
        }

        nowtime = time_us_64();
        status_rgb_poll(nowtime);
        if (nowtime-systime > clkdiv) {
            cpu.unibus.kw11.tick();
            systime = nowtime;
        }
    }
}

int startup()
{
    setup();
    while (1)
        loop();
}

void panic() {
    cpu.printstate();
    std::abort();
}
