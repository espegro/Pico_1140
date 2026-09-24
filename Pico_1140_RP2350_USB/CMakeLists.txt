cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD waveshare_rp2350_plus_16mb CACHE STRING "Target board")
endif()

include(pico_sdk_import.cmake)

project(Pico_1140_RP2350_USB C CXX ASM)
pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(DISK_IMAGE_PATH "${CMAKE_CURRENT_LIST_DIR}/images/Unix_V6.RK05"
    CACHE FILEPATH "RK05 image to embed in the UF2")
file(SIZE "${DISK_IMAGE_PATH}" DISK_IMAGE_SIZE)
if (DISK_IMAGE_SIZE GREATER 3145728)
    message(FATAL_ERROR "The embedded disk image does not fit below the journal")
endif()
configure_file(embedded_disk.S.in ${CMAKE_CURRENT_BINARY_DIR}/embedded_disk.S @ONLY)

if (TARGET tinyusb_device)
    add_executable(Pico_1140 avr11.cxx disasm.cxx getline.cxx kb11.cxx kl11.cxx kt11.cxx kw11.cxx lp11.cxx pc11.cxx Pico_1140.cxx rk11.cxx rl11.cxx unibus.cxx dl11.cxx fp11.cxx flash_disk.cxx status_rgb.cxx ${CMAKE_CURRENT_BINARY_DIR}/embedded_disk.S)

    # pull in common dependencies
    target_link_libraries(Pico_1140 pico_stdlib hardware_flash hardware_pio pico_flash)
    target_compile_options(Pico_1140 PRIVATE $<$<CONFIG:Release>:-O3>)
    pico_generate_pio_header(Pico_1140 ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

    # Keep the bundled RK05 image separate from the program and leave the top
    # 12 MB of the 16 MB device for its copy-on-write journal.
    target_link_options(Pico_1140 PRIVATE
        "-Wl,--section-start=.disk_image=0x10100000")

    # enable usb output, disable uart output
    pico_enable_stdio_usb(Pico_1140 1)
    pico_enable_stdio_uart(Pico_1140 1)

    # create map/bin/hex/uf2 file etc.
    pico_add_extra_outputs(Pico_1140)

    # add url via pico_set_program_url
#     example_auto_set_url(Pico_1140)
elseif(PICO_ON_DEVICE)
    message(WARNING "not building hello_usb because TinyUSB submodule is not initialized in the SDK")
endif()
add_compile_definitions(PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1)
add_compile_definitions(PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0)
