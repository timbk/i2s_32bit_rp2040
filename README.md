# i2s_32bit_rp2040
An up to 32bit I2S library for the RP2040 using PIO

WORK IN PROGRESS!

![PulseView waveform](https://github.com/timbk/i2s_32bit_rp2040/blob/main/miscellaneous/pictures/logic_analyzer.png?raw=true)

## Motivation

The library in [pico-extras I2S library](https://github.com/raspberrypi/pico-extras/tree/master/src/rp2_common/pico_audio_i2s) was not sufficient for me as it did not include the following features:

* [x] Flexible sample sizes >16 bit
* [ ] I2S receiver
* [ ] Generating an MCLK signal
* [ ] More control for real time applications

I also did not like the quite complex audio interface implemented in pico-extras. I'm trying to keep this interface simpler and more aimed towards signal generation instead of audio.

The library is designed to be compatible with the pinout of the pico-extras code at the time of writing this.

## Compiling

With pico-sdk environment variables set: Go to `testing/` and execute `./make.sh`. It should build the binary in `testing/build/`

## Flashing

Use the compiled UF2 file and copy it or flash with e.g. openOCD.

## Resources

* [Nicely documented pico I2S implementation](https://www.elektronik-labor.de/Raspberry/Pico13.html) (German)
* [Phillips I2S documentation](https://web.archive.org/web/20070102004400/http://www.nxp.com/acrobat_download/various/I2SBUS.pdf)
* [pico-extras I2S lib developement](https://github.com/raspberrypi/pico-extras/tree/master/src/rp2_common/pico_audio_i2s) and corresponding [example code](https://github.com/raspberrypi/pico-playground/tree/master/audio/sine_wave)
