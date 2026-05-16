cmake_minimum_required(VERSION 3.20)

project(MonomythClientDll LANGUAGES CXX)

add_library(dinput8 SHARED
    src/dllmain.cpp
    src/dinput_proxy.cpp
    src/logger.cpp
    src/fingerprint.cpp
    src/hook_manager.cpp
    src/packet_observer.cpp
    src/receive_dispatch_discovery.cpp
    src/runtime_capabilities.cpp
    dinput8.def
)

target_include_directories(dinput8 PRIVATE src)

target_compile_features(dinput8 PRIVATE cxx_std_20)

target_compile_definitions(dinput8 PRIVATE
    DIRECTINPUT_VERSION=0x0800
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    set_property(TARGET dinput8 PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

set_target_properties(dinput8 PROPERTIES
    OUTPUT_NAME dinput8
    PREFIX ""
)

target_link_libraries(dinput8 PRIVATE version)
