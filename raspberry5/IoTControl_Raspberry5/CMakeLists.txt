cmake_minimum_required(VERSION 3.16)

project(IoTControl_Raspberry5
    VERSION 1.0.0
    DESCRIPTION "TCP-based IoT control server and client"
    LANGUAGES C
)

find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# The existing sources use a GNU extension (arithmetic on void pointers).
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IOTCONTROL_ENABLE_WARNINGS "Enable common compiler warnings" ON)

function(iotcontrol_add_executable target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE Threads::Threads)

    if(IOTCONTROL_ENABLE_WARNINGS)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE
                -Wall
                -Wextra
                -Wpedantic
            )
        endif()
    endif()
endfunction()

iotcontrol_add_executable(iot_server iot_server.c)
iotcontrol_add_executable(iot_client iot_client.c)

# iot_server reads this file relative to its current working directory.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/idpasswd.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/idpasswd.txt"
    COPYONLY
)
