# 서버 콘솔 명령 (명령 한 줄 실행 / 그룹별 명령 처리)
# 서버 API(RServerControl.h)는 iot_server의 RSocket.c가 구현한다.
add_library(iot_server_command STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/RCommand.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/RCmdServer.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/RCmdBt.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/RCmdDb.c"
)
iotcontrol_configure_target(iot_server_command)
target_include_directories(iot_server_command PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)
