project(WebRTCClient VERSION 0.1 LANGUAGES CXX)

set(3RD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Multimedia WebSockets)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(OpenCV REQUIRED)

# ── OpenMP（macOS）────────────────────────────────────────
if(APPLE)
    if(EXISTS "/opt/homebrew/opt/libomp")
        set(OpenMP_C_FLAGS   "-Xpreprocessor -fopenmp")
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp")
        set(OpenMP_C_LIB_NAMES   "omp")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "/opt/homebrew/opt/libomp/lib/libomp.dylib")
    elseif(EXISTS "/usr/local/opt/libomp")
        set(OpenMP_C_FLAGS   "-Xpreprocessor -fopenmp")
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp")
        set(OpenMP_C_LIB_NAMES   "omp")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "/usr/local/opt/libomp/lib/libomp.dylib")
    endif()
endif()
find_package(OpenMP)

# ── FFmpeg ────────────────────────────────────────────────
set(FFMPEG_ROOT "/opt/homebrew/Cellar/ffmpeg@7/7.1.3")
include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

# ── ONNX Runtime ──────────────────────────────────────────
set(ONNXRUNTIME_DIR ${3RD_DIR}/onnxruntime-osx-arm64-1.23.2)
include_directories(${ONNXRUNTIME_DIR}/include)
link_directories(${ONNXRUNTIME_DIR}/lib)

# ── libwebrtc（仓库内预编译版本）──────────────────────────
# 提供完整 WebRTC 栈，含 AudioProcessing / VideoCodecs 等
set(LIBWEBRTC_DIR ${3RD_DIR}/libwebrtc)
set(LIBWEBRTC_INCLUDE_DIR ${LIBWEBRTC_DIR}/include)
set(LIBWEBRTC_LIB_DIR     ${LIBWEBRTC_DIR}/lib)

add_library(webrtc_native STATIC IMPORTED)
set_target_properties(webrtc_native PROPERTIES
    IMPORTED_LOCATION "${LIBWEBRTC_LIB_DIR}/libwebrtc.a"
    # 不设置 INTERFACE_INCLUDE_DIRECTORIES：libwebrtc 头文件仅供
    # chromium clang 编译的桥接文件使用，不应暴露给 Apple clang 编译
    # 的其他文件（bundled abseil 会遮蔽系统 abseil，引起 ABI 冲突）
)

# ── chromium bundled libc++（提供 std::__Cr::* 符号）──────
add_library(chromium_libcxx STATIC IMPORTED)
set_target_properties(chromium_libcxx PROPERTIES
    IMPORTED_LOCATION "${LIBWEBRTC_LIB_DIR}/libchromium_libcxx.a"
)

# ── chromium clang 桥接编译 ────────────────────────────────
# 以下文件直接调用 libwebrtc API，需与 libwebrtc.a 使用相同 ABI
# （chromium clang 18 + std::__Cr libc++）编译：
#   • WebRTCClient.cpp / WebRTCVideoSource.cpp — WebRTC 传输/视频层
#   • AudioProcesser.cpp                       — WebRTC APM 封装
#（VideoCapturer.cpp 使用 FFmpeg avfoundation，由 Apple clang 编译）
#
# 优先使用仓库内 prebuilt/*.o；缺失时用仓库内 bin/clang++ 重新编译。

set(LIBWEBRTC_PREBUILT_DIR ${LIBWEBRTC_DIR}/prebuilt)

# VideoCapturer.cpp 使用 FFmpeg avfoundation 输入实现（Apple clang，不需要 chromium 桥接）
# 原因：libwebrtc 在 macOS 上的 VideoCaptureModule 是空桩（CreateDeviceInfo 返回 null），
# 而 FFmpeg 抽象层在 macOS/Linux/Windows 上具有更好的兼容性。

set(WEBRTC_CLIENT_PREBUILT_O  "${LIBWEBRTC_PREBUILT_DIR}/WebRTCClient.cpp.o")
set(WEBRTC_VIDSRC_PREBUILT_O  "${LIBWEBRTC_PREBUILT_DIR}/WebRTCVideoSource.cpp.o")

if(EXISTS "${WEBRTC_CLIENT_PREBUILT_O}"
   AND EXISTS "${WEBRTC_VIDSRC_PREBUILT_O}")

    message(STATUS "Using prebuilt WebRTC bridge objects from ${LIBWEBRTC_PREBUILT_DIR}")
    set(WEBRTC_CLIENT_O  "${WEBRTC_CLIENT_PREBUILT_O}")
    set(WEBRTC_VIDSRC_O  "${WEBRTC_VIDSRC_PREBUILT_O}")
    add_custom_target(webrtc_bridge_objs)

else()
    message(STATUS "Prebuilt bridge objects not found, compiling with chromium clang")

    set(CHROMIUM_CLANG "${LIBWEBRTC_DIR}/bin/clang++")
    set(SDK "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.0.sdk")

    set(CHROMIUM_LIBCXX_INCLUDE     "${LIBWEBRTC_INCLUDE_DIR}/libc++_config")
    set(CHROMIUM_LIBCXX_SRC_INCLUDE "${LIBWEBRTC_INCLUDE_DIR}/libc++_src_include")

    set(WEBRTC_BRIDGE_FLAGS
        -std=c++17
        -arch arm64
        -mmacos-version-min=10.15
        -isysroot ${SDK}
        -nostdinc++
        -isystem ${CHROMIUM_LIBCXX_INCLUDE}
        -isystem ${CHROMIUM_LIBCXX_SRC_INCLUDE}
        -I${LIBWEBRTC_INCLUDE_DIR}
        -I${LIBWEBRTC_INCLUDE_DIR}/libyuv
        -I${CMAKE_CURRENT_SOURCE_DIR}/src
        -I${FFMPEG_ROOT}/include
        -I/opt/homebrew/include
        -DWEBRTC_POSIX -DWEBRTC_MAC
        -DABSL_ALLOCATOR_NOTHROW=1
        -DSPDLOG_NO_EXCEPTIONS
        -Wno-deprecated-declarations -Wno-unused-parameter
        -fno-rtti -fno-exceptions
        -g -O0
    )

    set(WEBRTC_BRIDGE_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/webrtc_bridge")
    file(MAKE_DIRECTORY "${WEBRTC_BRIDGE_OUT_DIR}")

    set(WEBRTC_CLIENT_O  "${WEBRTC_BRIDGE_OUT_DIR}/WebRTCClient.cpp.o")
    set(WEBRTC_VIDSRC_O  "${WEBRTC_BRIDGE_OUT_DIR}/WebRTCVideoSource.cpp.o")

    add_custom_command(
        OUTPUT  "${WEBRTC_CLIENT_O}"
        COMMAND ${CHROMIUM_CLANG} ${WEBRTC_BRIDGE_FLAGS}
                -c "${CMAKE_CURRENT_SOURCE_DIR}/src/WebRTCClient.cpp"
                -o "${WEBRTC_CLIENT_O}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/WebRTCClient.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/WebRTCClient.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/WebRTCVideoSource.h"
        COMMENT "Compiling WebRTCClient.cpp with chromium clang (std::__Cr ABI)"
        VERBATIM
    )

    add_custom_command(
        OUTPUT  "${WEBRTC_VIDSRC_O}"
        COMMAND ${CHROMIUM_CLANG} ${WEBRTC_BRIDGE_FLAGS}
                -c "${CMAKE_CURRENT_SOURCE_DIR}/src/WebRTCVideoSource.cpp"
                -o "${WEBRTC_VIDSRC_O}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/WebRTCVideoSource.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/WebRTCVideoSource.h"
        COMMENT "Compiling WebRTCVideoSource.cpp with chromium clang (std::__Cr ABI)"
        VERBATIM
    )

    add_custom_target(webrtc_bridge_objs
        DEPENDS "${WEBRTC_CLIENT_O}" "${WEBRTC_VIDSRC_O}"
    )
endif()

# ── 源文件 ────────────────────────────────────────────────
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE SRC_FILES    ${SRC_DIR}/*.cpp ${SRC_DIR}/*.cc ${SRC_DIR}/*.mm)
file(GLOB_RECURSE HEADER_FILES ${SRC_DIR}/*.h   ${SRC_DIR}/*.hpp)

# 从 Apple clang 编译列表中排除由 chromium clang 单独编译的文件
# requestPermissions.mm 使用 AVFoundation 请求权限（Apple clang），无需排除
list(REMOVE_ITEM SRC_FILES
    "${SRC_DIR}/WebRTCClient.cpp"
    "${SRC_DIR}/WebRTCVideoSource.cpp"
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${HEADER_FILES})

qt_add_executable(appWebRTCClient
    ${SRC_FILES}
    ${HEADER_FILES}
    "${WEBRTC_CLIENT_O}"
    "${WEBRTC_VIDSRC_O}"
)
add_dependencies(appWebRTCClient webrtc_bridge_objs)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# ── QML 模块 ─────────────────────────────────────────────
set(QML_DIR ${CMAKE_CURRENT_SOURCE_DIR}/qml)
file(GLOB_RECURSE QML_FILES_ABS ${QML_DIR}/*.qml)
set(QML_FILES_RELATIVE "")
foreach(qml_file ${QML_FILES_ABS})
    file(RELATIVE_PATH rel_path ${QML_DIR} ${qml_file})
    list(APPEND QML_FILES_RELATIVE qml/${rel_path})
endforeach()

qt_add_qml_module(appWebRTCClient
    URI WebRTCClient
    VERSION 1.0
    QML_FILES ${QML_FILES_RELATIVE}
    RESOURCES resource.qrc
)

set_target_properties(appWebRTCClient PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.yourcompany.appWebRTCClient"
    MACOSX_BUNDLE_BUNDLE_NAME "appWebRTCClient"
    MACOSX_BUNDLE_EXECUTABLE_NAME "appWebRTCClient"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

# ── 编译选项 ─────────────────────────────────────────────
target_compile_options(appWebRTCClient PRIVATE
    -stdlib=libc++
    -Wno-deprecated-declarations
    -Wno-unused-parameter
)

target_include_directories(appWebRTCClient PRIVATE
    "/usr/local/include"
    # libwebrtc 头文件不加入 Apple clang 的 include 路径（避免 abseil ABI 冲突）
    # 所有直接调用 libwebrtc API 的文件均已由 chromium clang 编译
)

# ── 链接库 ────────────────────────────────────────────────
target_link_libraries(appWebRTCClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Multimedia
    Qt6::WebSockets
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    # libwebrtc 静态库 + 视频编解码器工厂库
    webrtc_native
    "${LIBWEBRTC_LIB_DIR}/libbuiltin_video_encoder_factory.a"
    "${LIBWEBRTC_LIB_DIR}/libbuiltin_video_decoder_factory.a"
    "${LIBWEBRTC_LIB_DIR}/librtc_internal_video_codecs.a"
    "${LIBWEBRTC_LIB_DIR}/librtc_simulcast_encoder_adapter.a"
    # chromium libc++（提供 std::__Cr::* 实现，与 libwebrtc.a ABI 匹配）
    chromium_libcxx
    # macOS 系统框架（libwebrtc 依赖）
    "-framework Foundation"
    "-framework CoreFoundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework AVFoundation"
    "-framework VideoToolbox"
    "-framework CoreGraphics"
    "-framework CoreServices"
    "-framework SystemConfiguration"
    "-framework IOKit"
    "-lc++"
)

# FFmpeg：
#   avformat / avcodec / avdevice — VideoCapturer 使用 avfoundation 输入采集摄像头
#   avutil                        — AVFrame 结构体（显示/AI 流水线）
#   swscale                       — 图像格式转换（Capturer、Controller、GestureInfer、SegInfer）
#   swresample                    — 音频重采样（AsrClient）
target_link_libraries(appWebRTCClient PRIVATE
    avformat avcodec avdevice avutil swscale swresample
)

target_link_libraries(appWebRTCClient PRIVATE
    onnxruntime ${OpenCV_LIBS}
)

if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: ${OpenMP_CXX_VERSION}")
    target_link_libraries(appWebRTCClient PRIVATE OpenMP::OpenMP_CXX)
else()
    message(WARNING "OpenMP not found, building without OpenMP support")
endif()

target_compile_definitions(appWebRTCClient PRIVATE
    CMAKE_CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    WEBRTC_POSIX
    WEBRTC_MAC
)

include(GNUInstallDirs)
install(TARGETS appWebRTCClient
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
