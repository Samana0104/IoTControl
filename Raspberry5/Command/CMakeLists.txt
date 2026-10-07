# 서버 관리용 CLI 명령 (stdin 입력 처리 / 명령 테이블)
# 서버 API(IotServerControl.h)는 iot_server의 IotSocket.c가 구현한다.
add_library(iot_server_command STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/IotServerCli.c"
)
iotcontrol_configure_target(iot_server_command)
target_include_directories(iot_server_command PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)
