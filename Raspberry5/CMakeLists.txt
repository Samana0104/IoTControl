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
    "${CMAKE_CURRENT_SOURCE_DIR}/RMain.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RBluetooth.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RDatabase.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RDatabaseCommand.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RSocket.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Command/Src/RCommand.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Command/Src/RCmdServer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Command/Src/RCmdBt.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Command/Src/RCmdDb.c"
)
iotcontrol_configure_target(iot_server)
target_include_directories(iot_server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Command/Inc")
target_compile_definitions(iot_server PRIVATE IOT_BT_PAIR_AGENT_DEFAULT="${CMAKE_CURRENT_BINARY_DIR}/bluetooth_pair_agent.py")
target_link_libraries(iot_server PRIVATE Threads::Threads OpenSSL::SSL PkgConfig::MARIADB PkgConfig::SODIUM PkgConfig::BLUEZ)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/bluetooth_pair_agent.py"
    "${CMAKE_CURRENT_BINARY_DIR}/bluetooth_pair_agent.py"
    COPYONLY
)

add_executable(iot_client
    "${CMAKE_CURRENT_SOURCE_DIR}/iot_client.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RClient.c"
)
iotcontrol_configure_target(iot_client)
target_link_libraries(iot_client PRIVATE OpenSSL::SSL PkgConfig::SODIUM)

add_executable(iot_bt_client
    "${CMAKE_CURRENT_SOURCE_DIR}/iot_bt_client.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/RBluetooth.c"
)
iotcontrol_configure_target(iot_bt_client)
target_link_libraries(iot_bt_client PRIVATE PkgConfig::BLUEZ)

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
