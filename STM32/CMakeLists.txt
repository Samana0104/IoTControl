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

# 펌웨어 버전은 IoTControl/Common/SDefine.h의 FIRMWARE_VERSION (.bin 파일 이름용으로 여기서 읽음)
# SDefine.h가 바뀌면 다시 configure해서 이름도 따라 바뀜
set(FIRMWARE_DEFINE_FILE ${CMAKE_SOURCE_DIR}/IoTControl/Common/SDefine.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FIRMWARE_DEFINE_FILE})
file(STRINGS ${FIRMWARE_DEFINE_FILE} FIRMWARE_VERSION_LINE REGEX "^#define FIRMWARE_VERSION ")
string(REGEX MATCH "[0-9]+" FIRMWARE_VERSION "${FIRMWARE_VERSION_LINE}")
if(NOT FIRMWARE_VERSION)
    message(FATAL_ERROR "FIRMWARE_VERSION not found in ${FIRMWARE_DEFINE_FILE}")
endif()
message("Firmware version: ${FIRMWARE_VERSION}")

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    # 앱은 부트로더 뒤 0x08004000 (common/IoTFirmware.h의 FIRMWARE_APP_ADDRESS)에서 실행
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=0x00004000U
)

# SystemInit(system_stm32f4xx.c)은 STM32_Drivers 라이브러리에서 따로 컴파일되므로 여기에도 넣어야 VTOR이 옮겨짐
# (없으면 디버거로 앱을 바로 실행할 때 SysTick이 0x08000000 벡터로 가서 멈춤, cmake/stm32cubemx는 CubeMX가 덮어씀)
target_compile_definitions(STM32_Drivers PRIVATE
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
