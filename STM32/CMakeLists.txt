cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME IoTControl)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

file(GLOB_RECURSE USER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/IoTControl/*.c
    # 4개 환경 공용 패킷 코드
    ${CMAKE_SOURCE_DIR}/../common/*.c
)
file(GLOB USER_INCLUDE_DIRS LIST_DIRECTORIES true
    ${CMAKE_SOURCE_DIR}/IoTControl/*/Inc
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${USER_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${USER_INCLUDE_DIRS}
    IoTControl/Common
    ${CMAKE_SOURCE_DIR}/../common
)

# 펌웨어 버전: 업데이트할 때마다 올림 (cmake -DFIRMWARE_VERSION=2 또는 CMakePresets)
set(FIRMWARE_VERSION 1 CACHE STRING "Application firmware version stored in FirmwareInfo")

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    FIRMWARE_VERSION=${FIRMWARE_VERSION}U
    # 앱은 부트로더 뒤 0x08004000 (common/IoTFirmware.h의 FIRMWARE_APP_ADDRESS)에서 실행
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=0x00004000U
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# 업데이트용 바이너리: build/.../IoTControl-v<버전>.bin → 공유 폴더 firmware/stm32/에 복사 후 서버 'fw push'
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}-v${FIRMWARE_VERSION}.bin
    COMMENT "Firmware image: ${CMAKE_PROJECT_NAME}-v${FIRMWARE_VERSION}.bin"
)
