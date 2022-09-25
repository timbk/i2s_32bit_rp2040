#pragma once

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/clocks.h>

#define I2S_PIO_PROGRAM_LENGTH 8

#include <exception>
#define _USE_MATH_DEFINES
#include <cmath>

// This can be changed to DMA IRQ1 if needed
#define I2S_DMA_IRQ 0

/**
 * @brief small class that provides the I2S buffer and pattern
 *  can generate some basic waveforms and quick updating
 */
class PATTERN_BUFFER {
public:
    enum PATTERN{
        CONST = 0,
        SINE=1,
        TRI=2,
        SQUARE=3,
    };

    int32_t *pattern_buffer;
private:
    uint buffer_size, pattern_length;
    PATTERN pattern;
    int32_t offset, amplitude;

private:
    /// update the pattern buffer
    void update_pattern_buffer() {
        switch(pattern) {
            case PATTERN::CONST:
                for(uint i=0; i<pattern_length*2; ++i)
                    pattern_buffer[i] = offset;
                break;
            case PATTERN::SINE:
                for(uint i=0; i<pattern_length*2; ++i)
                    pattern_buffer[i] = sin(2*M_PI*(((int)(i/2))/(float)pattern_length))*amplitude + offset;
                break;
            case PATTERN::TRI:
                for(uint i=0; i<pattern_length; ++i) {
                    if(i < (pattern_length/2)) {
                        pattern_buffer[i*2] = ((float)amplitude) * i*2/pattern_length + offset;
                    } else {
                        pattern_buffer[i*2] = ((float)amplitude) * (pattern_length-i-1)*2/pattern_length + offset;
                    }
                    pattern_buffer[i*2+1] = pattern_buffer[i*2];
                }
                break;
            case PATTERN::SQUARE:
                for(uint i=0; i<pattern_length*2; ++i)
                    pattern_buffer[i] = (((uint)(i/2)) >= (pattern_length/2))*amplitude + offset;
                break;
            default: // handle undefined pattern values gracefully
                printf("pattern_buffer: Pattern not implemented\n");
                for(uint i=0; i<pattern_length*2; ++i)
                    pattern_buffer[i] = 0;
        }
    }
public:
    PATTERN_BUFFER(uint _buffer_size) : buffer_size(_buffer_size){
        pattern_buffer = new int32_t[buffer_size*2];

        pattern_length = buffer_size;
        pattern = PATTERN::CONST;
        offset = 0;
        amplitude = 0;
    }

    ~PATTERN_BUFFER() {
        delete [] pattern_buffer;
    }

    void set_pattern(PATTERN new_pattern) { pattern = new_pattern; update_pattern_buffer(); }
    void set_offset(int32_t new_offset) { offset = new_offset; update_pattern_buffer(); }
    void set_amplitude(int32_t new_amplitude) { amplitude = new_amplitude; update_pattern_buffer(); }
    uint set_pattern_length(uint new_pattern_length) { return pattern_length = new_pattern_length < buffer_size ? new_pattern_length : buffer_size; update_pattern_buffer(); }

    /**
     * @brief change setting generator
     *
     * @return new pattern length (might be clipped due to buffer size
     */
    uint set_pattern(PATTERN new_pattern, int32_t new_offset, int32_t new_amplitude, uint new_pattern_length) {
        pattern = new_pattern;
        offset = new_offset;
        amplitude = new_amplitude;
        pattern_length = new_pattern_length < buffer_size ? new_pattern_length : buffer_size;

        update_pattern_buffer();

        return pattern_length;
    }
	
    // TODO: low frequency signal interpolation with second buffer?
    int32_t *get_next_buffer(uint &buffer_len) {
        buffer_len = pattern_length*2;
        return pattern_buffer;
    }

    void print_pattern_config() {
        printf("pattern=%u, offset=%li, amplitude=%li, length=%u\n", pattern, offset, amplitude, pattern_length);
    }
};

/**
 * @brief I2S signal generator using PIO
 * This is an I2S transmitter implementation for the RP2040 PIO.
 * It is built for up to 32 bits and 
 */
class I2S_CONTROLLER {
private:
    // PIO and DMA settings
    const PIO I2S_PIO;
    const uint8_t I2S_PIO_SM;
    const uint8_t I2S_DMA_CHANNEL;

    // general setup
    const uint8_t BIT_DEPTH;

    // Pin settings
    const uint8_t PIN_DATA; 
    const uint8_t PIN_CLK_BASE; 

    // status
    uint32_t clock_divider_setting;

    // buffers for PIO code
    uint16_t i2s_pio_code[I2S_PIO_PROGRAM_LENGTH];
    struct pio_program i2s_program_header;
    uint pio_program_offset;
public:
    PATTERN_BUFFER pattern_buffer;
private:
    /**
     * @brief set up PIO as we need it
     * @param divider clock divider setting, see set_pio_divider() for more
     */
    void configure_pio(uint32_t divider);

    /**
     * @brief generate PIO program and load it into PIO memory
     * @return the program offset
     */
    uint generate_pio_program();

    void configure_dma();
public:
    /**
     * @brief constructor
     * @param pattern_buffer_size size of the pattern buffer in samples
     * @param pin_data pin number of the I2S data output pin
     * @param pin_clock_base pin number of the BCLK clock pin
     *                       LRCK is at pin (clock_base_pin + 1)
     *                       (chosen to be pin compatible to the pico-extras I2S implementation)
     * @param bit_depth I2S bit count per sample (valid values are 2..32)
     * @param i2s_pio either pio0 or pio1, can be adjusted to avoid conflictls with other PIO programs
     * @param i2s_pio_sm The state machine to be used, can be adjusted to avoid conflictls with other PIO programs (0..3)
     * @param i2s_dma_channel The DMA channel to be used, can be adjusted to avoid conflictls (0..11)
     *
     * clock divider setting defaults to 100*256 (sample_rate = 125e6/(clock_divider/256)/bit_depth)
     */
    I2S_CONTROLLER (
        uint pattern_buffer_size,
        uint8_t pin_data,
        uint8_t pin_clock_base,
        uint8_t bit_depth = 32,
        PIO i2s_pio = pio0,
        uint8_t i2s_pio_sm = 0,
        uint8_t i2s_dma_channel = 0
    );   

    ~I2S_CONTROLLER();

    /**
     * @brief set the clock divider for the I2S PIO state machiene
     * @param divider divider value from the RP2040 system clock includeing 256 fractional value (so 0x100 is a divider of 1, 0xA00 is a divider of 10)
     */
    void set_pio_divider(uint16_t divider);

    /// @brief returns the current sample rate in Hz (assuming perfect crystal)
    float get_sample_rate() const;

    /// enable PIO and start DMA
    void start_i2s();

    /// just a wrapper for PATTERN_BUFFER::set_pattern()
    uint set_pattern(PATTERN_BUFFER::PATTERN pattern, int32_t offset, int32_t amplitude, uint pattern_length) {
        return pattern_buffer.set_pattern(pattern, offset, amplitude, pattern_length);
    }
};
