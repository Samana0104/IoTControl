cmake_minimum_required(VERSION 3.16)

project(IoTControl_PC VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(IoTControl_PC
    Core/Src/Pmain.cpp
    Core/Src/Pmainwindow.cpp
    Core/Src/Pmainwindowserver.cpp
    Core/Src/Pmainwindowlogin.cpp
    Core/Src/Pmainwindowtransition.cpp
    Core/Src/Pconnectioncanvas.cpp
    Core/Src/Pfeaturedetailswidget.cpp
    Core/Src/Pdashboardwidget.cpp
    Core/Src/Pfanchartwidget.cpp
    Core/Src/Pfanrotorwidget.cpp
    Core/Src/Pbluetoothdialog.cpp
    Core/Src/Pgamingtheme.cpp
    Core/Inc/Pmainwindow.h
    Core/Inc/Pconnectioncanvas.h
    Core/Inc/Pfeaturedetailswidget.h
    Core/Inc/Pdashboardwidget.h
    Core/Inc/Pfanchartwidget.h
    Core/Inc/Pfanrotorwidget.h
    Core/Inc/Pbluetoothdialog.h
    Core/Inc/Pgamingtheme.h
    screens/Pmainwindow.ui
    screens/Pdashboard.ui
    screens/Pbluetoothdialog.ui
)

target_include_directories(IoTControl_PC PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc")
target_link_libraries(IoTControl_PC PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

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
