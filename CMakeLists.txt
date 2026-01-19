cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(TJ3FW)

target_sources(app PRIVATE 
    src/main.c 
    src/atm90e26.c
    src/led.c
    src/protocol.c
    src/mqtt.c
)
target_include_directories(app PRIVATE inc)
