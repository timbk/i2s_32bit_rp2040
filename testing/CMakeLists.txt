cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)
 
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
project(testing_i2s)
pico_sdk_init()

add_executable(testing_i2s
    main.cpp
)

target_compile_options(testing_i2s PRIVATE -Wall ) # These all throw too many warning from SDK: -Wpedantic -Werror -Wextra

set_property(TARGET testing_i2s PROPERTY CXX_STANDARD 20)

target_link_libraries(testing_i2s PRIVATE
    pico_stdlib
    hardware_pio
)
pico_add_extra_outputs(testing_i2s)

# configure stdio output
# pico_enable_stdio_usb(testing_i2s 1)
pico_enable_stdio_uart(testing_i2s 1)
