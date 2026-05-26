cmake_minimum_required(VERSION 3.20)
project(WebRTCClientServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network WebSockets)
add_executable(WebRTCClientServer
        main.cpp
        SignalingServer.cpp
        SignalingServer.h
        IceServerConfig.cpp
        IceServerConfig.h
)

target_link_libraries(WebRTCClientServer
        PRIVATE
        Qt6::Gui
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
)
