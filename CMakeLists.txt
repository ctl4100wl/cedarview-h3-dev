cmake_minimum_required(VERSION 3.21)

project(CedarView VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Network Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-video-1.0
)

qt_add_executable(cedarview
    src/main.cpp
    src/camera.h
    src/configstore.h
    src/configstore.cpp
    src/cameradialog.h
    src/cameradialog.cpp
    src/rtspscanner.h
    src/rtspscanner.cpp
    src/videotile.h
    src/videotile.cpp
    src/gridview.h
    src/gridview.cpp
    src/mainwindow.h
    src/mainwindow.cpp
)

target_link_libraries(cedarview PRIVATE
    Qt6::Network
    Qt6::Widgets
    PkgConfig::GSTREAMER
)

target_compile_definitions(cedarview PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

include(GNUInstallDirs)
configure_file(
    packaging/cedarview.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/cedarview.desktop
    @ONLY
)
install(TARGETS cedarview
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cedarview.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
