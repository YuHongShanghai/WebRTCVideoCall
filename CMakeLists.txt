cmake_minimum_required(VERSION 3.19)
project(WebRTCClient VERSION 0.1 LANGUAGES CXX)

set(CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.9.3/macos")

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Multimedia WebSockets)
find_package(libdatachannel REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

set(FFMPEG_ROOT "/opt/homebrew/Cellar/ffmpeg/7.1.1_3")
include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

# webrtc audio processing
# https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing/
set(WEBRTC_AP_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/webrtc-audio-processing)

add_library(webrtc_ap SHARED IMPORTED)
set_target_properties(webrtc_ap PROPERTIES
        IMPORTED_LOCATION
        ${WEBRTC_AP_PREFIX}/lib/libwebrtc-audio-processing-2.dylib
        INTERFACE_INCLUDE_DIRECTORIES
        ${WEBRTC_AP_PREFIX}/include/webrtc-audio-processing
)

# CPP FILE
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/*.cpp ${SRC_DIR}/*.cc)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})

file(GLOB_RECURSE HEADER_FILES
        ${SRC_DIR}/*.h
        ${SRC_DIR}/*.hpp
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${HEADER_FILES})

qt_add_executable(appWebRTCClient
        ${SRC_FILES}
        ${HEADER_FILES}
)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# QML MODULE
set(QML_DIR ${CMAKE_CURRENT_SOURCE_DIR}/qml)
file(GLOB_RECURSE QML_FILES_ABS ${QML_DIR}/*.qml)
set(QML_FILES_RELATIVE "")
foreach(qml_file ${QML_FILES_ABS})
    file(RELATIVE_PATH rel_path ${QML_DIR} ${qml_file})
    list(APPEND QML_FILES_RELATIVE qml/${rel_path})
    message(STATUS qml/${rel_path})
endforeach()
source_group("qml" FILES ${QML_FILES_ABS})

qt_add_qml_module(appWebRTCClient
    URI WebRTCClient
    VERSION 1.0
    QML_FILES ${QML_FILES_RELATIVE}
    RESOURCES resource.qrc
)

set_target_properties(appWebRTCClient PROPERTIES
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
)

target_include_directories(appWebRTCClient PRIVATE "/usr/local/include")

target_link_libraries(appWebRTCClient
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Multimedia
        Qt6::WebSockets
        LibDataChannel::LibDataChannel
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

target_link_libraries(appWebRTCClient PRIVATE
        avdevice
        avformat
        avcodec
        avfilter
        avutil
        swresample
        swscale
)

target_include_directories(appWebRTCClient PRIVATE ${WEBRTC_AP_PREFIX}/include)
target_link_libraries(appWebRTCClient PRIVATE webrtc_ap)

include(GNUInstallDirs)
install(TARGETS appWebRTCClient
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
