cmake_minimum_required(VERSION 3.16)

project(IoTControl_Raspberry5
    VERSION 1.0.0
    DESCRIPTION "TCP-based IoT control server and client"
    LANGUAGES C
)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MARIADB REQUIRED IMPORTED_TARGET libmariadb)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(BLUEZ REQUIRED IMPORTED_TARGET bluez)
# HC-05 PIN 페어링 에이전트 (BlueZ D-Bus)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IOTCONTROL_ENABLE_WARNINGS "Enable common compiler warnings" ON)

function(iotcontrol_configure_target target)
    # 헤더 탐색 경로 등록
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Driver/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Utils/Inc"
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

# 4개 환경(Raspberry5 / STM32 / Arduino / PC) 공용 패킷 코드
file(GLOB IOT_COMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../common/*.c")
add_library(IoTCommon STATIC ${IOT_COMMON_SOURCES})
iotcontrol_configure_target(IoTCommon)
target_include_directories(IoTCommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../common")

# 새 .c 파일은 다음 빌드 때 자동으로 포함된다 (CONFIGURE_DEPENDS)
file(GLOB IOT_SERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Driver/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Command/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Packet/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Session/Src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utils/Src/*.c"
)
# RClient.c는 iot_client 전용
list(REMOVE_ITEM IOT_SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RClient.c")

add_executable(IotServer "${CMAKE_CURRENT_SOURCE_DIR}/Main.c" ${IOT_SERVER_SOURCES})

iotcontrol_configure_target(IotServer)
target_include_directories(IotServer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Command/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Packet/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Session/Inc"
)
target_link_libraries(IotServer PRIVATE IoTCommon Threads::Threads OpenSSL::SSL PkgConfig::MARIADB PkgConfig::SODIUM PkgConfig::BLUEZ PkgConfig::SYSTEMD)

add_executable(iot_client
    "${CMAKE_CURRENT_SOURCE_DIR}/iot_client.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RClient.c"
)
iotcontrol_configure_target(iot_client)
target_link_libraries(iot_client PRIVATE IoTCommon OpenSSL::SSL PkgConfig::SODIUM)

add_executable(iot_bt_client
    "${CMAKE_CURRENT_SOURCE_DIR}/iot_bt_client.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Driver/Src/RBluetooth.c"
)
iotcontrol_configure_target(iot_bt_client)
target_link_libraries(iot_bt_client PRIVATE PkgConfig::BLUEZ PkgConfig::SYSTEMD)

add_executable(iot_password_hash
    "${CMAKE_CURRENT_SOURCE_DIR}/password_hash.c"
)
iotcontrol_configure_target(iot_password_hash)
target_link_libraries(iot_password_hash PRIVATE PkgConfig::SODIUM)

# iot_server reads this file relative to its current working directory.
# configure_file(
#     "${CMAKE_CURRENT_SOURCE_DIR}/idpasswd.txt"
#     "${CMAKE_CURRENT_BINARY_DIR}/idpasswd.txt"
#     COPYONLY
# )
