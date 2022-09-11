#include <stdio.h>
#include <string.h>

#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <hardware/uart.h>
#include <hardware/clocks.h>

#include "i2s.hpp"

#define LED_PIN 25

#define PIN_I2S_DOUT 20
#define PIN_I2S_CLOCK_BASE 17

int main(void) {
    // configure UART
    stdio_uart_init_full(uart0, 115200, 0, 1);

    // configure main loop LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0); 

    // create i2s instance
    I2S_TX i2s_tx(PIN_I2S_DOUT, PIN_I2S_CLOCK_BASE, 32);

    i2s_tx.start_i2s();

    uint32_t u = 0;
    while (1) {
        gpio_put(LED_PIN, (time_us_32() % 1000000) < 50000);
        // pio_sm_put_blocking(I2S_PIO, I2S_PIO_SM, (u++)%128 + 1000000);
    }
}
