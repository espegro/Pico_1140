#define CFG_TUD_VENDOR 1
#define PICO_STDIO_ENABLE_CRLF_SUPPORT 0

#include <stdio.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "status_rgb.h"

int startup();

int main() {
    // RP2350 is specified for 150 MHz. Overclocking can be enabled after the
    // new internal-flash storage path has been validated on real hardware.
    set_sys_clock_khz(150000, true);
    stdio_init_all();
    status_rgb_init();
    status_rgb_boot_animation();

    gpio_set_pulls(1, true, false);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    gpio_set_function(20, GPIO_FUNC_UART);
    gpio_set_function(21, GPIO_FUNC_UART);
    uart_init(uart0, 9600);
    uart_init(uart1, 9600);

    while (!stdio_usb_connected())
        sleep_ms(10);
    sleep_ms(500);

    printf("\r\nPico_1140 RP2350 internal-flash build\r\n");
    printf("Boot image: Unix_V6.RK05\r\n");
    return startup();
}
