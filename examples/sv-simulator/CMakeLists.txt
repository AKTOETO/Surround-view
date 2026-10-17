cmake_minimum_required(VERSION 3.20)
project(sv-simulator LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Qml Quick Network QuickControls2)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 6.3 QUIET COMPONENTS Quick3D)
    if(TARGET Qt6::Quick3D)
        set(SV_SIMULATOR_QUICK3D_AVAILABLE 1)
    else()
        set(SV_SIMULATOR_QUICK3D_AVAILABLE 0)
        message(STATUS "Qt Quick 3D not found; sv-simulator will omit the driving preview")
    endif()
else()
    set(SV_SIMULATOR_QUICK3D_AVAILABLE 0)
endif()

add_library(sv-simulator-research STATIC
    research_controller.cpp research_controller.hpp
    research_draft.cpp research_draft.hpp research_results.cpp research_results.hpp)
target_include_directories(sv-simulator-research PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sv-simulator-research PUBLIC sv-research Qt${QT_VERSION_MAJOR}::Core)

add_executable(sv-simulator
    main.cpp
    ipc_discovery.cpp
    ipc_discovery.hpp
    telemetry_model.cpp
    telemetry_metrics.cpp
    telemetry_files.cpp
    telemetry_model.hpp
    simulator_bridge.cpp
    simulator_bridge.hpp
    runtime_settings.cpp
    runtime_settings.hpp
    configuration_editor.hpp
    configuration_editor.cpp
    configuration_draft.cpp
    configuration_surface.cpp
    configuration_operation.cpp
    simulator.qrc
)

target_include_directories(sv-simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sv-simulator PRIVATE
    sv-simulator-research
    sv-client-lib
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::QuickControls2
    Qt${QT_VERSION_MAJOR}::Network
)
if(SV_SIMULATOR_QUICK3D_AVAILABLE)
    target_link_libraries(sv-simulator PRIVATE Qt6::Quick3D)
endif()
target_compile_definitions(sv-simulator PRIVATE
    SV_SIMULATOR_QUICK3D_AVAILABLE=${SV_SIMULATOR_QUICK3D_AVAILABLE})

install(TARGETS sv-simulator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
