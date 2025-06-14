cmake_minimum_required(VERSION 3.12)

set(NAME picovision-mp3)  # TODO: Change me!

include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

include(picovision_import.cmake)

# Include required libraries
# This assumes `pimoroni-pico` is stored alongside your project

# I2C and SPI
include(common/pimoroni_i2c)
include(common/pimoroni_bus)

# Graphics stuff
include(libraries/jpegdec/jpegdec)
include(libraries/pico_graphics/pico_graphics)

add_subdirectory(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src)
add_subdirectory(picomp3lib/src)

# Add your source files
add_executable(${NAME}
    main.cpp # TODO: Add source files here!
    picow_bt_example_common.c
    a2dp_source_demo.c
    picomp3lib/interface/music_file.c
    fixed_fft.cpp
)

# Increase CLKDIV to 4, because we're going to run the system at 320 MHz, and
# the flash gets rather unhappy running that fast
pico_clone_default_boot_stage2(custom_boot2)
target_compile_definitions(custom_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(${NAME} custom_boot2)

# TODO: Don't forget to link the libraries you need!
target_link_libraries(${NAME}
pico_stdlib
    picovision
    pico_graphics
    pimoroni_i2c
    pico_multicore
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_cyw43
    pico_cyw43_arch_threadsafe_background
    pico_btstack_sbc_encoder
    picomp3lib
)

target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(${NAME} PRIVATE
    USB_HOST=1
    SDCARD_SPI_BUS=spi1
    SDCARD_PIN_SPI0_CS=15
    SDCARD_PIN_SPI0_SCK=10
    SDCARD_PIN_SPI0_MOSI=11
    SDCARD_PIN_SPI0_MISO=12
    CYW43_LWIP=0
    CYW43_PIO_CLOCK_DIV_INT=5
    CYW43_SPI_PROGRAM_NAME=spi_gap0_sample1
)

target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/config
    )

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

pico_enable_stdio_uart(${NAME} 0)
pico_enable_stdio_usb(${NAME} 1)

target_link_options(${NAME} PRIVATE
    -Wl,--print-memory-usage
)

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
