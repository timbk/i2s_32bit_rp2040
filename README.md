# i2s_32bit_rp2040
An up to 32bit I2S library for the RP2040 using PIO

## Compiling

With pico-sdk environment variables set: Go to `testing/` and execute `./make.sh`. It should build the binary in `testing/build/`

## Flashing

Use the compiled UF2 file and copy it or flash with e.g. openOCD.

## Resources

* [Nicely documented pico I2S implementation](https://www.elektronik-labor.de/Raspberry/Pico13.html) (German)
* [Phillips I2S documentation](https://web.archive.org/web/20070102004400/http://www.nxp.com/acrobat_download/various/I2SBUS.pdf)
* [pico-extras I2S lib developement](https://github.com/raspberrypi/pico-extras/tree/master/src/rp2_common/pico_audio_i2s) and corresponding [example code](https://github.com/raspberrypi/pico-playground/tree/master/audio/sine_wave)
