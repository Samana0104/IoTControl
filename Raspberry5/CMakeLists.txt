cmake_minimum_required(VERSION 3.16)

project(IoTControl_Raspberry5
    VERSION 1.0.0
    DESCRIPTION "TCP-based IoT control server and client"
    LANGUAGES C
)

find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IOTCONTROL_ENABLE_WARNINGS "Enable common compiler warnings" ON)

function(iotcontrol_configure_target target)
    # 헤더 탐색 경로 등록
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
    )

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

add_executable(iot_server
    "${CMAKE_CURRENT_SOURCE_DIR}/IotMain.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/IotSocket.c"
)
iotcontrol_configure_target(iot_server)
target_link_libraries(iot_server PRIVATE Threads::Threads)

add_executable(iot_client
    "${CMAKE_CURRENT_SOURCE_DIR}/iot_client.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/IotClient.c"
)
iotcontrol_configure_target(iot_client)

# iot_server reads this file relative to its current working directory.
# configure_file(
#     "${CMAKE_CURRENT_SOURCE_DIR}/idpasswd.txt"
#     "${CMAKE_CURRENT_BINARY_DIR}/idpasswd.txt"
#     COPYONLY
# )
