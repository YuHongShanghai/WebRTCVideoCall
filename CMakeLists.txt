cmake_minimum_required(VERSION 3.31)
project(WebRTCVideoCall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.9.3/macos")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_subdirectory(WebRTCClient)
add_subdirectory(WebRTCClientServer)
