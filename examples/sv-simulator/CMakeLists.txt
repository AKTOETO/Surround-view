cmake_minimum_required(VERSION 3.20)
project(sv-simulator LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Qml Quick Network)

add_executable(sv-simulator
    main.cpp
    ipc_discovery.cpp
    ipc_discovery.hpp
    simulator_bridge.cpp
    simulator_bridge.hpp
    simulator.qrc
)

target_include_directories(sv-simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sv-simulator PRIVATE
    sv-client-lib
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Network
)

install(TARGETS sv-simulator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
