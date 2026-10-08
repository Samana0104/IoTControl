cmake_minimum_required(VERSION 3.22)

# STM32 부트로더 (섹터 0, 16KB). 앱(../)과 링커 스크립트가 달라 별도 프로젝트로 빌드함.
# ../cmake/gcc-arm-none-eabi.cmake가 ${CMAKE_SOURCE_DIR}/STM32F411xx_FLASH.ld를 쓰므로
# 이 폴더의 STM32F411xx_FLASH.ld(FLASH = 0x08000000, 16K)가 적용됨.
# ST-Link로 한 번만 구우면 됨: IoTBootloader.bin → 0x08000000

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_PROJECT_NAME IoTBootloader)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
enable_language(C ASM)

add_executable(${CMAKE_PROJECT_NAME}
    Src/Boot.c
    # 앱과 같은 시작 코드 (SystemInit은 VTOR를 건드리지 않음: USER_VECT_TAB_ADDRESS 미정의)
    ${CMAKE_SOURCE_DIR}/../startup_stm32f411xe.s
    ${CMAKE_SOURCE_DIR}/../Core/Src/system_stm32f4xx.c
    # 배치, CRC32, 이미지 검사
    ${CMAKE_SOURCE_DIR}/../../common/IoTFirmware.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/../Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${CMAKE_SOURCE_DIR}/../Drivers/CMSIS/Include
    ${CMAKE_SOURCE_DIR}/../../common
)

# HAL 없이 CMSIS 레지스터만 사용 (USE_HAL_DRIVER 미정의)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    STM32F411xE
)

target_link_libraries(${CMAKE_PROJECT_NAME} ${TOOLCHAIN_LINK_LIBRARIES})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Bootloader image: ${CMAKE_PROJECT_NAME}.bin (flash at 0x08000000)"
)
