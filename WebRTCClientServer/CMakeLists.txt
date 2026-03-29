project(WebRTCClientServer)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network WebSockets)
add_executable(WebRTCClientServer main.cpp SignalingServer.cpp SignalingServer.h)

target_link_libraries(WebRTCClientServer
        PRIVATE
        Qt6::Gui
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
)