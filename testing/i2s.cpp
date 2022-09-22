#include <stdio.h>

#include "i2s.hpp"

struct I2S_SETTINGS{
    bool initialized;
    PATTERN_BUFFER *pattern_buffer;
};

// list of pattern buffers for the individual DMA channels
I2S_SETTINGS i2s_settings[12] = {
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
    {false, NULL},
};

// irq handler for DMA
void __isr __time_critical_func(audio_i2s_dma_irq_handler)() {
    dma_channel_config c;
    uint buffer_len = 0;
    int32_t *new_buffer;

    for(uint dma_channel=0; dma_channel<12; ++dma_channel) {
        if (i2s_settings[dma_channel].initialized && dma_irqn_get_channel_status(I2S_DMA_IRQ, dma_channel)) {
            dma_irqn_acknowledge_channel(I2S_DMA_IRQ, dma_channel);

            // from audio_start_dma_transfer
            c = dma_get_channel_config(dma_channel);
            channel_config_set_read_increment(&c, true);
            dma_channel_set_config(dma_channel, &c, false);

            new_buffer = i2s_settings[dma_channel].pattern_buffer->get_next_buffer(buffer_len);
            dma_channel_transfer_from_buffer_now(dma_channel, new_buffer, buffer_len);
        }
    }
}

// ------------- I2S_TX Class ---------------- //

I2S_TX::I2S_TX (
    uint pattern_buffer_size,
    uint8_t pin_data,
    uint8_t pin_clock_base,
    uint8_t bit_depth,
    PIO i2s_pio,
    uint8_t i2s_pio_sm,
    uint8_t i2s_dma_channel
    ) : pattern_buffer(pattern_buffer_size), I2S_PIO(i2s_pio), I2S_PIO_SM(i2s_pio_sm), I2S_DMA_CHANNEL(i2s_dma_channel), BIT_DEPTH(bit_depth),PIN_DATA(pin_data), PIN_CLK_BASE(pin_clock_base) {

    configure_pio(2500); // correct for 96 kHz 32bit @ 120 MHz system clock, 2 clock steps per bit in PIO
    configure_dma();

    // pass data to DMA hanlder
    i2s_settings[I2S_DMA_CHANNEL].pattern_buffer = &pattern_buffer;
    i2s_settings[I2S_DMA_CHANNEL].initialized = true;
}

I2S_TX::~I2S_TX () {
    i2s_settings[I2S_DMA_CHANNEL].initialized = false;
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
    uint program_offset = generate_pio_program();

    // call the PIO block setup function
    // audio_i2s_program_init(I2S_PIO, I2S_PIO_SM, program_offset, PIN_DATA, PIN_CLK_BASE);
    pio_sm_config sm_config = pio_get_default_sm_config();
    sm_config_set_wrap(&sm_config, program_offset + 0, program_offset + I2S_PIO_PROGRAM_LENGTH-1); // set program length and wrapping
    sm_config_set_sideset(&sm_config, 2, false, false); // set side pin count

    sm_config_set_out_pins(&sm_config, PIN_DATA, 1);
    sm_config_set_sideset_pins(&sm_config, PIN_CLK_BASE);
    sm_config_set_out_shift(&sm_config, false, true, BIT_DEPTH); // enables autopull with correct bit per sample count

    pio_sm_init(I2S_PIO, I2S_PIO_SM, program_offset, &sm_config);

    pio_gpio_init(I2S_PIO, PIN_DATA);
    pio_gpio_init(I2S_PIO, PIN_CLK_BASE);
    pio_gpio_init(I2S_PIO, PIN_CLK_BASE+1);

    uint pin_mask = (1u << PIN_DATA) | (3u << PIN_CLK_BASE);
    pio_sm_set_pindirs_with_mask(I2S_PIO, I2S_PIO_SM, pin_mask, pin_mask);
    pio_sm_set_pins(I2S_PIO, I2S_PIO_SM, 0);

    pio_sm_exec(I2S_PIO, I2S_PIO_SM, pio_encode_jmp(program_offset + I2S_PIO_PROGRAM_LENGTH-1));

    // set clock settings
    set_pio_divider(divider);
}

uint I2S_TX::generate_pio_program() {
    // TODO: this could be a lot prettier using the pio_encode_*() functions
    uint32_t bit_depth_value = (BIT_DEPTH-2) & 0x1F;

    // generate the PIO code
    i2s_pio_code[0] = 0x7001;                   //  0: out    pins, 1         side 2
    i2s_pio_code[1] = 0x1840,                   //  1: jmp    x--, 0          side 3
    i2s_pio_code[2] = 0x6001,                   //  2: out    pins, 1         side 0
    i2s_pio_code[3] = 0xe820 | bit_depth_value, //  3: set    x, BIT_DEPTH-2  side 1
    i2s_pio_code[4] = 0x6001,                   //  4: out    pins, 1         side 0
    i2s_pio_code[5] = 0x0844,                   //  5: jmp    x--, 4          side 1
    i2s_pio_code[6] = 0x7001,                   //  6: out    pins, 1         side 2
    i2s_pio_code[7] = 0xf820 | bit_depth_value, //  7: set    x, BIT_DEPTH-2  side 3

    // set PIO header
    i2s_program_header.instructions = i2s_pio_code;
    i2s_program_header.length = 8;
    i2s_program_header.origin = -1;

    pio_program_offset = pio_add_program(I2S_PIO, &i2s_program_header);
    return pio_program_offset;
}

void I2S_TX::configure_dma() {
    dma_channel_claim(I2S_DMA_CHANNEL);
    dma_channel_config dma_config = dma_channel_get_default_config(I2S_DMA_CHANNEL);

    channel_config_set_dreq(&dma_config, pio_get_dreq(I2S_PIO, I2S_PIO_SM, true));
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32); // 8, 16 and 32 bit DMAs are possible
    dma_channel_configure(I2S_DMA_CHANNEL,
        &dma_config,
        &I2S_PIO->txf[I2S_PIO_SM],
        NULL,
        0,
        false
    );

    irq_add_shared_handler(DMA_IRQ_0 + I2S_DMA_IRQ, audio_i2s_dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    dma_irqn_set_channel_enabled(I2S_DMA_IRQ, I2S_DMA_CHANNEL, 1);
}

void I2S_TX::start_i2s() {
    uint buffer_len = 0;
    int32_t *new_buffer;

    printf("i2s interrupt started\n");
    pio_sm_set_enabled(I2S_PIO, I2S_PIO_SM, 1);
    irq_set_enabled(DMA_IRQ_0 + I2S_DMA_IRQ, 1);

    new_buffer = i2s_settings[I2S_DMA_CHANNEL].pattern_buffer->get_next_buffer(buffer_len);
    dma_channel_transfer_from_buffer_now(I2S_DMA_CHANNEL, new_buffer, buffer_len);
}
