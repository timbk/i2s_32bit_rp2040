#pragma once

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/clocks.h>

/**
 * @brief I2S signal generator using PIO
 * This is an I2S transmitter implementation for the RP2040 PIO.
 * It is built for up to 32 bits and 
 */
class I2S_TX {
private:
    // PIO and DMA settings
    const PIO I2S_PIO;
    const uint8_t I2S_PIO_SM;
    const uint8_t I2S_DMA_CHANNEL;
    const uint8_t I2S_DMA_IRQ;

    // general setup
    const uint8_t BIT_DEPTH;

    // Pin settings
    const uint8_t PIN_DATA; 
    const uint8_t PIN_CLK_BASE; 

    // status
    uint32_t clock_divider_setting;
private:
    /**
     * @brief set up PIO as we need it
     * @param divider clock divider setting, see set_pio_divider() for more
     */
    void configure_pio(uint32_t divider);

    void configure_dma();
public:
    /**
     * @brief constructor
     * @param pin_data pin number of the I2S data output pin
     * @param pin_clock_base pin number of the BCLK clock pin
     *                       LRCK is at pin (clock_base_pin + 1)
     *                       (chosen to be pin compatible to the pico-extras I2S implementation)
     * @param bit_depth I2S bit count per sample (valid values are 2..32)
     * @param i2s_pio either pio0 or pio1, can be adjusted to avoid conflictls with other PIO programs
     * @param i2s_pio_sm The state machine to be used, can be adjusted to avoid conflictls with other PIO programs
     * @param i2s_dma_channel The DMA channel to be used, can be adjusted to avoid conflictls
     * @param i2s_dma_irq The DMA interrupt to be used, can be adjusted to avoid conflictls
     *
     * clock divider setting defaults to 100*256 (sample_rate = 125e6/(clock_divider/256)/bit_depth)
     */
    I2S_TX (
        uint8_t pin_data,
        uint8_t pin_clock_base,
        uint8_t bit_depth = 32,
        PIO i2s_pio = pio0,
        uint8_t i2s_pio_sm = 0,
        uint8_t i2s_dma_channel = 0,
        uint8_t i2s_dma_irq = 0
        );   

    /**
     * @brief set the clock divider for the I2S PIO state machiene
     * @param divider divider value from the RP2040 system clock includeing 256 fractional value (so 0x100 is a divider of 1, 0xA00 is a divider of 10)
     */
    void set_pio_divider(uint16_t divider);

    /// @brief returns the current sample rate in Hz (assuming perfect crystal)
    float get_sample_rate() const;

    /// enable PIO and start DMA
    void start_i2s();
};
