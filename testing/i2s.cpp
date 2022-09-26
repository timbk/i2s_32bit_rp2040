#include <stdio.h>

#include <pico/time.h>

#include "i2s.hpp"

struct I2S_SETTINGS{
    bool tx_initialized;
    bool rx_initialized;
    PATTERN_BUFFER *pattern_buffer;
    int32_t *rx_buffer; // TODO: replace with real buffer
    uint rx_buffer_size;
};

// list of pattern buffers for the individual DMA channels
I2S_SETTINGS i2s_settings[12] = {
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
    {false, false, NULL, NULL, 0},
};

uint last_buffer_len=1; // TODO remove

int32_t temporaerer_buffer_chooser = 0;
int32_t temporaerer_buffer[2][4096];

// irq handler for DMA
void __isr __time_critical_func(i2s_dma_irq_handler)() {
    uint buffer_len = 0;
    int32_t *new_buffer;
    bool did_something = false;

    for(uint dma_channel=0; dma_channel<12; ++dma_channel) {
        if (i2s_settings[dma_channel].tx_initialized && dma_irqn_get_channel_status(I2S_DMA_IRQ, dma_channel)) {
            puts("TX");
            // clear IRQ
            dma_irqn_acknowledge_channel(I2S_DMA_IRQ, dma_channel);

            new_buffer = i2s_settings[dma_channel].pattern_buffer->get_next_buffer(buffer_len);
            dma_channel_transfer_from_buffer_now(dma_channel, new_buffer, buffer_len);
            last_buffer_len = buffer_len;
            did_something = true;
        } else if (i2s_settings[dma_channel].rx_initialized && dma_irqn_get_channel_status(I2S_DMA_IRQ, dma_channel)) {
            puts("RX");
            // clear IRQ
            dma_irqn_acknowledge_channel(I2S_DMA_IRQ, dma_channel);

            // TODO: do something with new data / swap buffer
            temporaerer_buffer_chooser += 1;
            temporaerer_buffer_chooser %= 2;
            dma_channel_transfer_to_buffer_now(dma_channel, temporaerer_buffer[temporaerer_buffer_chooser], last_buffer_len); // TODO: fix
            did_something = true;
        }
    }
    if(not did_something) {
        puts("did nothing");
    }
}

// ------------- I2S_CONTROLLER Class ---------------- //

I2S_CONTROLLER::I2S_CONTROLLER (
    uint pattern_buffer_size,
    uint8_t pin_data_base,
    uint8_t pin_clock_base,
    I2S_CONTROLLER_MODE trx_mode,
    uint8_t bit_depth,
    PIO i2s_pio,
    uint8_t i2s_pio_sm,
    uint8_t i2s_dma_channel
    ) : mode(trx_mode), I2S_PIO(i2s_pio), I2S_PIO_SM(i2s_pio_sm), BIT_DEPTH(bit_depth),PIN_DATA_BASE(pin_data_base), PIN_CLK_BASE(pin_clock_base), pattern_buffer(pattern_buffer_size) {

    if(mode == I2S_CONTROLLER_MODE::TRX) {
        I2S_DMA_CHANNEL_TX = i2s_dma_channel;
        I2S_DMA_CHANNEL_RX = i2s_dma_channel+1;
    } else {
        // only one gets used
        I2S_DMA_CHANNEL_TX = i2s_dma_channel;
        I2S_DMA_CHANNEL_RX = i2s_dma_channel;
    }

    configure_pio(2500); // correct for 96 kHz 32bit @ 120 MHz system clock, 2 clock steps per bit in PIO
    configure_dma();

    // pass data to DMA hanlder
    if(mode != I2S_CONTROLLER_MODE::RX) {
        i2s_settings[I2S_DMA_CHANNEL_TX].pattern_buffer = &pattern_buffer;
        i2s_settings[I2S_DMA_CHANNEL_TX].tx_initialized = true;
    }

    if(mode != I2S_CONTROLLER_MODE::TX) {
        input_buffer = new int32_t [pattern_buffer_size*2];

        i2s_settings[I2S_DMA_CHANNEL_RX].rx_buffer = input_buffer;
        i2s_settings[I2S_DMA_CHANNEL_RX].rx_initialized = true;
    }
}

I2S_CONTROLLER::~I2S_CONTROLLER () {
    // both I2S_DMA_CHANNEL_* are always valid values, so this is safe
    i2s_settings[I2S_DMA_CHANNEL_TX].tx_initialized = false;
    i2s_settings[I2S_DMA_CHANNEL_RX].rx_initialized = false;

    if(mode != I2S_CONTROLLER_MODE::RX) {
        dma_channel_unclaim(I2S_DMA_CHANNEL_TX);
    }

    if(mode != I2S_CONTROLLER_MODE::TX) {
        delete [] input_buffer;
        dma_channel_unclaim(I2S_DMA_CHANNEL_RX);
    }
}

void I2S_CONTROLLER::set_pio_divider(uint16_t divider) {
    clock_divider_setting = divider;
    pio_sm_set_clkdiv_int_frac(I2S_PIO, I2S_PIO_SM, divider >> 8, divider & 0xff);
}

float I2S_CONTROLLER::get_sample_rate() const {
    //                System clock            Fractional divider value       Bit count   left/right samples
    float samp_rate = clock_get_hz(clk_sys) / (clock_divider_setting/256.) / BIT_DEPTH / 2;

    if(mode == I2S_CONTROLLER_MODE::TRX) {
        samp_rate /= 2; // double clock cycles needed for TX and RX simultaneously
    }
    return samp_rate;
}

void I2S_CONTROLLER::configure_pio(uint32_t divider) {
    const uint8_t sideset_pin_cnt = (mode == I2S_CONTROLLER_MODE::TRX) ? 3 : 2;
    uint pin_mask=0, pin_mask_dir=0;

    // load program
    uint program_offset = generate_pio_program();

    // call the PIO block setup function
    pio_sm_config sm_config = pio_get_default_sm_config();
    sm_config_set_wrap(&sm_config, program_offset + 0, program_offset + I2S_PIO_PROGRAM_LENGTH-1); // set program length and wrapping
    sm_config_set_sideset(&sm_config, sideset_pin_cnt, false, false); // set side pin count

    switch(mode) {
        case I2S_CONTROLLER_MODE::TX:
            sm_config_set_out_pins(&sm_config, PIN_DATA_BASE, 1);

            pin_mask = (1u << PIN_DATA_BASE) | (3u << PIN_CLK_BASE);
            pin_mask_dir = pin_mask;

            sm_config_set_out_shift(&sm_config, false, true, BIT_DEPTH); // enables autopull with correct bit per sample count
            break;
        case I2S_CONTROLLER_MODE::RX:
            sm_config_set_in_pins(&sm_config, PIN_DATA_BASE);

            pin_mask = (1u << PIN_DATA_BASE) | (3u << PIN_CLK_BASE);
            pin_mask_dir = 0 | (3u << PIN_CLK_BASE);

            sm_config_set_in_shift(&sm_config, false, true, BIT_DEPTH); // enables autopull with correct bit per sample count
            break;
        case I2S_CONTROLLER_MODE::TRX:
            sm_config_set_in_pins(&sm_config, PIN_DATA_BASE);
            sm_config_set_out_pins(&sm_config, PIN_DATA_BASE+1, 1);

            pin_mask = (3u << PIN_DATA_BASE) | (7u << PIN_CLK_BASE);
            pin_mask_dir = (1u << PIN_DATA_BASE) | (7u << PIN_CLK_BASE);

            sm_config_set_out_shift(&sm_config, false, true, BIT_DEPTH); // enables autopull with correct bit per sample count
            sm_config_set_in_shift(&sm_config, false, true, BIT_DEPTH);  // enables autopull with correct bit per sample count
            break;
    }
    sm_config_set_sideset_pins(&sm_config, PIN_CLK_BASE);

    pio_sm_init(I2S_PIO, I2S_PIO_SM, program_offset, &sm_config);

    pio_gpio_init(I2S_PIO, PIN_DATA_BASE);
    pio_gpio_init(I2S_PIO, PIN_CLK_BASE);
    pio_gpio_init(I2S_PIO, PIN_CLK_BASE+1);
    if(mode == I2S_CONTROLLER_MODE::TRX) {
        pio_gpio_init(I2S_PIO, PIN_DATA_BASE+1);
        pio_gpio_init(I2S_PIO, PIN_CLK_BASE+2);
    }

    pio_sm_set_pindirs_with_mask(I2S_PIO, I2S_PIO_SM, pin_mask_dir, pin_mask);
    pio_sm_set_pins(I2S_PIO, I2S_PIO_SM, 0); // initialize all SM pins with 0

    pio_sm_exec(I2S_PIO, I2S_PIO_SM, pio_encode_jmp(program_offset + I2S_PIO_PROGRAM_LENGTH-1));

    // set clock settings
    set_pio_divider(divider);
}

uint I2S_CONTROLLER::generate_pio_program() {
    uint32_t bit_depth_value = (BIT_DEPTH-2) & 0x1F;

    // NOTE: Take care of I2S_PIO_MAX_PROGRAM_LENGTH when you adjust this!
    if(mode == I2S_CONTROLLER_MODE::TX) {
        // TX PIO ASM
        i2s_pio_code[0] = pio_encode_out(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 2); //  0: out    pins, 1         side 2
        i2s_pio_code[1] = pio_encode_jmp_x_dec(0)                               | pio_encode_sideset(2, 3); //  1: jmp    x--, 0          side 3
        i2s_pio_code[2] = pio_encode_out(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 0); //  2: out    pins, 1         side 0
        i2s_pio_code[3] = pio_encode_set(pio_src_dest::pio_x, bit_depth_value)  | pio_encode_sideset(2, 1); //  3: set    x, BIT_DEPTH-2  side 1
        i2s_pio_code[4] = pio_encode_out(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 0); //  4: out    pins, 1         side 0
        i2s_pio_code[5] = pio_encode_jmp_x_dec(4)                               | pio_encode_sideset(2, 1); //  5: jmp    x--, 4          side 1
        i2s_pio_code[6] = pio_encode_out(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 2); //  6: out    pins, 1         side 2
        i2s_pio_code[7] = pio_encode_set(pio_src_dest::pio_x, bit_depth_value)  | pio_encode_sideset(2, 3); //  7: set    x, BIT_DEPTH-2  side 3

        I2S_PIO_PROGRAM_LENGTH = 8;
    } else if(mode == I2S_CONTROLLER_MODE::RX) {
        // RX PIO ASM
        i2s_pio_code[0] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 2); //  0: out    pins, 1         side 2
        i2s_pio_code[1] = pio_encode_jmp_x_dec(0)                               | pio_encode_sideset(2, 3); //  1: jmp    x--, 0          side 3
        i2s_pio_code[2] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 0); //  2: out    pins, 1         side 0
        i2s_pio_code[3] = pio_encode_set(pio_src_dest::pio_x, bit_depth_value)  | pio_encode_sideset(2, 1); //  3: set    x, BIT_DEPTH-2  side 1
        i2s_pio_code[4] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 0); //  4: out    pins, 1         side 0
        i2s_pio_code[5] = pio_encode_jmp_x_dec(4)                               | pio_encode_sideset(2, 1); //  5: jmp    x--, 4          side 1
        i2s_pio_code[6] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 2); //  6: out    pins, 1         side 2
        i2s_pio_code[7] = pio_encode_set(pio_src_dest::pio_x, bit_depth_value)  | pio_encode_sideset(2, 3); //  7: set    x, BIT_DEPTH-2  side 3

        I2S_PIO_PROGRAM_LENGTH = 8;
    } else if(mode == I2S_CONTROLLER_MODE::TRX) {
        // TRX PIO ASM
        // This implementation delays for 1/4th of a bit to keep the clock symmetric
        // runs half as fast as TX/RX
        i2s_pio_code[ 0] = pio_encode_out(pio_src_dest::pio_pins, 1)            | pio_encode_sideset(2, 6);                        //  0: out    pins, 1         side 2
        i2s_pio_code[ 1] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 6);                        //  1: in     pins, 1         side 2
        i2s_pio_code[ 2] = pio_encode_jmp_x_dec(0)                              | pio_encode_sideset(2, 7) | pio_encode_delay(1);  //  2: jmp    x--, 0          side 3 delay 1

        i2s_pio_code[ 3] = pio_encode_out(pio_src_dest::pio_pins, 1)            | pio_encode_sideset(2, 0);                        //  3: out    pins, 1         side 0
        i2s_pio_code[ 4] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 0);                        //  4: in     pins, 1         side 0
        i2s_pio_code[ 5] = pio_encode_set(pio_src_dest::pio_x, bit_depth_value) | pio_encode_sideset(2, 1) | pio_encode_delay(1);  //  5: set    x, BIT_DEPTH-2  side 1 delay 1

        i2s_pio_code[ 6] = pio_encode_out(pio_src_dest::pio_pins, 1)            | pio_encode_sideset(2, 0);                        //  6: out    pins, 1         side 0
        i2s_pio_code[ 7] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 0);                        //  7: in     pins, 1         side 0
        i2s_pio_code[ 8] = pio_encode_jmp_x_dec(6)                              | pio_encode_sideset(2, 1) | pio_encode_delay(1);  //  8: jmp    x--, 6          side 1 delay 1

        i2s_pio_code[ 9] = pio_encode_out(pio_src_dest::pio_pins, 1)            | pio_encode_sideset(2, 6);                        //  9: out    pins, 1         side 2
        i2s_pio_code[10] = pio_encode_in(pio_src_dest::pio_pins, 1)             | pio_encode_sideset(2, 6);                        // 10: in     pins, 1         side 2
        i2s_pio_code[11] = pio_encode_set(pio_src_dest::pio_x, bit_depth_value) | pio_encode_sideset(2, 7) | pio_encode_delay(1);  // 12: set    x, BIT_DEPTH-2  side 3 delay 1

        I2S_PIO_PROGRAM_LENGTH = 12;
    }

    // set PIO header
    i2s_program_header.instructions = i2s_pio_code;
    i2s_program_header.length = I2S_PIO_PROGRAM_LENGTH;
    i2s_program_header.origin = -1;

    pio_program_offset = pio_add_program(I2S_PIO, &i2s_program_header);
    return pio_program_offset;
}

void I2S_CONTROLLER::configure_dma_channel(uint dma_channel, bool is_tx) {
    // configure DMA channel
    printf("Configuring DMA channel %d for i2s\n", dma_channel);
    dma_channel_claim(dma_channel);
    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel);

    channel_config_set_dreq(&dma_config, pio_get_dreq(I2S_PIO, I2S_PIO_SM, is_tx));
    if(is_tx) {
        channel_config_set_read_increment(&dma_config, true);
        channel_config_set_write_increment(&dma_config, false);
    } else {    
        channel_config_set_write_increment(&dma_config, true);
        channel_config_set_read_increment(&dma_config, false); // REQUIRED (I don't get why, but must be set like this for RX to work
    }

    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32); // 8, 16 and 32 bit DMAs are possible

    if(is_tx) {
        dma_channel_configure(dma_channel,
            &dma_config,
            &I2S_PIO->txf[I2S_PIO_SM],
            NULL,
            0,
            false
        );
    } else {
        dma_channel_configure(dma_channel,
            &dma_config,
            NULL,
            &I2S_PIO->rxf[I2S_PIO_SM],
            0,
            false
        );
    }

    dma_irqn_set_channel_enabled(I2S_DMA_IRQ, dma_channel, 1);
}

void I2S_CONTROLLER::configure_dma() {
    irq_add_shared_handler(DMA_IRQ_0 + I2S_DMA_IRQ, i2s_dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);

    switch(mode) {
        case I2S_CONTROLLER_MODE::TX:
            configure_dma_channel(I2S_DMA_CHANNEL_TX, true);
            break;
        case I2S_CONTROLLER_MODE::RX:
            configure_dma_channel(I2S_DMA_CHANNEL_RX, false);
            break;
        case I2S_CONTROLLER_MODE::TRX:
            configure_dma_channel(I2S_DMA_CHANNEL_TX, true);
            configure_dma_channel(I2S_DMA_CHANNEL_RX, false);
            break;
    }

}

void I2S_CONTROLLER::start_i2s() {
    uint buffer_len = 0;
    int32_t *new_buffer;

    printf("i2s interrupt started\n");
    pio_sm_set_enabled(I2S_PIO, I2S_PIO_SM, 1);
    irq_set_enabled(DMA_IRQ_0 + I2S_DMA_IRQ, 1);

    if(mode != I2S_CONTROLLER_MODE::RX) {
        new_buffer = i2s_settings[I2S_DMA_CHANNEL_TX].pattern_buffer->get_next_buffer(buffer_len);
        last_buffer_len = buffer_len;
        dma_channel_transfer_from_buffer_now(I2S_DMA_CHANNEL_TX, new_buffer, buffer_len);
    }
    
    if(mode != I2S_CONTROLLER_MODE::TX) {
        puts("starting RX DMA");
        // dma_channel_transfer_to_buffer_now(I2S_DMA_CHANNEL_RX, input_buffer, 4);
        dma_channel_set_trans_count(I2S_DMA_CHANNEL_RX, 2, false);
        dma_channel_set_write_addr(I2S_DMA_CHANNEL_RX, temporaerer_buffer[temporaerer_buffer_chooser], true);
    }
}
