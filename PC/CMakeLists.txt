cmake_minimum_required(VERSION 3.16)

project(IoTControl_PC VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

set(PC_UI_SOURCES
    Core/Src/Pmainwindow.cpp
    Core/Src/Pmainwindowserver.cpp
    Core/Src/Pmainwindowlogin.cpp
    Core/Src/Pserverconnection.cpp
    Core/Inc/Pserverconnection.h
    Core/Src/Pserverlogin.cpp
    Core/Inc/Pserverlogin.h
    Core/Inc/Ploginresult.h
    Core/Inc/Pdhtrecord.h
    Core/Src/Pserverdhtquery.cpp
    Core/Inc/Pserverdhtquery.h
    Core/Src/Pmainwindowdata.cpp
    Panel/Paccesspaneltransition.cpp
    Panel/Paccesspanel.cpp
    Panel/Paccesspanel.h
    Panel/Pdashboardpanel.cpp
    Panel/Pdashboardpanel.h
    Panel/Pbluetoothpanel.cpp
    Panel/Pbluetoothpanel.h
    widget/Pconnectioncanvas.cpp
    widget/Pfeaturedetailswidget.cpp
    Core/Src/Pdashboardwidget.cpp
    widget/Pfanchartwidget.cpp
    widget/Pfanrotorwidget.cpp
    Core/Src/Pbluetoothdialog.cpp
    widget/Pgamingtheme.cpp
    Core/Inc/Pmainwindow.h
    widget/Pconnectioncanvas.h
    widget/Pfeaturedetailswidget.h
    Core/Inc/Pdashboardwidget.h
    widget/Pfanchartwidget.h
    widget/Pfanrotorwidget.h
    Core/Inc/Pbluetoothdialog.h
    widget/Pgamingtheme.h
    screens/Pmainwindow.ui
    screens/Pdashboard.ui
    screens/Pbluetoothdialog.ui
    ../common/IoTPacketCodec.c
    ../common/IoTPacketStream.c
)

qt_add_executable(IoTControl_PC Core/Src/Pmain.cpp ${PC_UI_SOURCES})

# UI 문구/레이아웃 변경도 해당 Panel 구현을 다시 컴파일합니다.
set_source_files_properties(Panel/Paccesspanel.cpp Panel/Paccesspaneltransition.cpp
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/screens/Pmainwindow.ui")
set_source_files_properties(Panel/Pdashboardpanel.cpp
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/screens/Pdashboard.ui")
set_source_files_properties(Panel/Pbluetoothpanel.cpp
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/screens/Pbluetoothdialog.ui")

target_include_directories(IoTControl_PC PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Panel"
    "${CMAKE_CURRENT_SOURCE_DIR}/widget"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common"
)
target_link_libraries(IoTControl_PC PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

option(IOT_BUILD_TESTS "Build TCP connection integration tests" OFF)
if(IOT_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(server_connection_tests
        tests/Pserverconnectiontest.cpp
        ${PC_UI_SOURCES}
    )
    target_include_directories(server_connection_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Panel"
        "${CMAKE_CURRENT_SOURCE_DIR}/widget"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
    )
    target_link_libraries(server_connection_tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
    set_target_properties(server_connection_tests PROPERTIES
        AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/screens"
    )
    add_test(NAME server_connection_tests COMMAND server_connection_tests -o -,txt)

    qt_add_executable(server_login_tests
        tests/Pserverlogintest.cpp
        ${PC_UI_SOURCES}
    )
    target_include_directories(server_login_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Panel"
        "${CMAKE_CURRENT_SOURCE_DIR}/widget"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
    )
    target_link_libraries(server_login_tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
    set_target_properties(server_login_tests PROPERTIES
        AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/screens"
    )
    add_test(NAME server_login_tests COMMAND server_login_tests -o -,txt)

    find_package(Qt6 REQUIRED COMPONENTS Sql)
    qt_add_executable(dht_query_tests tests/Pdhtquerytest.cpp ${PC_UI_SOURCES} ../Raspberry5/Packet/Src/RPacketDhtQuery.c ../Raspberry5/Packet/Src/RPacketDht.c)
    target_include_directories(dht_query_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/serverstub"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Panel"
        "${CMAKE_CURRENT_SOURCE_DIR}/widget"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Raspberry5/Packet/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Raspberry5/Database/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Raspberry5/Session/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Raspberry5/Driver/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Raspberry5/Utils/Inc"
    )
    target_compile_definitions(dht_query_tests PRIVATE RLOG_LEVEL=0)
    target_link_libraries(dht_query_tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Test Qt6::Sql)
    set_target_properties(dht_query_tests PROPERTIES AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/screens")
    add_test(NAME dht_query_tests COMMAND dht_query_tests -o -,txt)
    if(WIN32)
        get_target_property(QT_TEST_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(QT_TEST_BIN_DIR "${QT_TEST_QMAKE_EXECUTABLE}" DIRECTORY)
        set_tests_properties(dht_query_tests PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${QT_TEST_BIN_DIR}/../plugins")
    endif()
endif()

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${QT_BIN_DIR}"
        NO_DEFAULT_PATH
        REQUIRED
    )

    add_custom_command(TARGET IoTControl_PC POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${QT_BIN_DIR};$ENV{PATH}"
                "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                "$<TARGET_FILE:IoTControl_PC>"
        COMMENT "Deploying Qt runtime DLLs and plugins"
        VERBATIM
    )
endif()

set_target_properties(IoTControl_PC PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/screens"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS IoTControl_PC
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
