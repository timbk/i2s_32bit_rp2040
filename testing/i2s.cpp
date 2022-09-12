#include <stdio.h>

#include "i2s.hpp"
#include "audio_i2s.pio.h" // TODO: check if we need this (maybe assemble it during startup with correct bit-depth))

// TODO: remove this
const uint dummy_buffer_len = 16;
// uint32_t dummy_buffer[dummy_buffer_len]={0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3, 0xF3};
// uint32_t dummy_buffer[dummy_buffer_len]={0x00000000, 0x0000340F, 0x00005F1E, 0x000079BB, 0x00007F4B, 0x00006ED9, 0x00004B3B, 0x00001A9C, 0xFFFFE564, 0xFFFFB4C5, 0xFFFF9127, 0xFFFF80B5, 0xFFFF8645, 0xFFFFA0E2, 0xFFFFCBF1, 0x00000000};
uint32_t dummy_buffer[dummy_buffer_len]={0x00000000, 0x30FBC54C, 0x5A827999, 0x7641AF3B, 0x7FFFFFFF, 0x7641AF3B, 0x5A827999, 0x30FBC54C, 0x00000000, 0xCF043AB4, 0xA57D8667, 0x89BE50C5, 0x80000001, 0x89BE50C5, 0xA57D8667, 0xCF043AB4 };

struct I2S_SETTINGS{
    bool initialized;
    uint8_t dma_irq;
    uint8_t dma_channel;
};

I2S_SETTINGS i2s_settings = {false, 0, 0};

// irq handler for DMA
void __isr __time_critical_func(audio_i2s_dma_irq_handler)() {
    if(not i2s_settings.initialized)
        return;

    if (dma_irqn_get_channel_status(i2s_settings.dma_irq, i2s_settings.dma_channel)) {
        dma_irqn_acknowledge_channel(i2s_settings.dma_irq, i2s_settings.dma_channel);

        // from audio_start_dma_transfer
        dma_channel_config c = dma_get_channel_config(i2s_settings.dma_channel);
        channel_config_set_read_increment(&c, true);
        dma_channel_set_config(i2s_settings.dma_channel, &c, false);

        dma_channel_transfer_from_buffer_now(i2s_settings.dma_channel, dummy_buffer, dummy_buffer_len);
    }
}

// ------------- I2S_TX Class ---------------- //

I2S_TX::I2S_TX (
    uint8_t pin_data,
    uint8_t pin_clock_base,
    uint8_t bit_depth,
    PIO i2s_pio,
    uint8_t i2s_pio_sm,
    uint8_t i2s_dma_channel,
    uint8_t i2s_dma_irq
    ) : I2S_PIO(i2s_pio), I2S_PIO_SM(i2s_pio_sm), I2S_DMA_CHANNEL(i2s_dma_channel), I2S_DMA_IRQ(i2s_dma_irq), BIT_DEPTH(bit_depth),PIN_DATA(pin_data), PIN_CLK_BASE(pin_clock_base) {

    configure_pio(2500); // correct for 96 kHz 32bit @ 120 MHz system clock, 2 clock steps per bit in PIO
    configure_dma();

    // pass data to DMA hanlder
    i2s_settings.dma_channel = I2S_DMA_CHANNEL;
    i2s_settings.dma_irq = I2S_DMA_IRQ;
    i2s_settings.initialized = true;
}

void I2S_TX::set_pio_divider(uint16_t divider) {
    clock_divider_setting = divider;
    pio_sm_set_clkdiv_int_frac(I2S_PIO, I2S_PIO_SM, divider >> 8, divider & 0xff);
}

float I2S_TX::get_sample_rate() const {
    //     System clock            Fractional divider value       Bit count   left/right samples
    return clock_get_hz(clk_sys) / (clock_divider_setting/256.) / BIT_DEPTH / 2;
}

void I2S_TX::configure_pio(uint32_t divider) {
    // load program
    uint program_offset = pio_add_program(I2S_PIO, &audio_i2s_program);
    // call the PIO block setup function
    audio_i2s_program_init(I2S_PIO, I2S_PIO_SM, program_offset, PIN_DATA, PIN_CLK_BASE);
    // set clock settings
    set_pio_divider(divider);
}

void I2S_TX::configure_dma() {
    dma_channel_claim(I2S_DMA_CHANNEL);
    dma_channel_config dma_config = dma_channel_get_default_config(I2S_DMA_CHANNEL);

    channel_config_set_dreq(&dma_config, pio_get_dreq(I2S_PIO, I2S_PIO_SM, true));
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32); // 8, 16 and 32 bit DMAs are possible
    dma_channel_configure(I2S_DMA_CHANNEL,
        &dma_config,
        &I2S_PIO->txf[I2S_PIO_SM],  // dest
        NULL, // src
        0, // count
        false // trigger
    );

    irq_add_shared_handler(DMA_IRQ_0 + I2S_DMA_IRQ, audio_i2s_dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    dma_irqn_set_channel_enabled(I2S_DMA_IRQ, I2S_DMA_CHANNEL, 1);
}

void I2S_TX::start_i2s() {
    printf("i2s interrupt started\n");
    pio_sm_set_enabled(I2S_PIO, I2S_PIO_SM, 1);
    irq_set_enabled(DMA_IRQ_0 + I2S_DMA_IRQ, 1);
    dma_channel_transfer_from_buffer_now(I2S_DMA_CHANNEL, dummy_buffer, dummy_buffer_len);
}
