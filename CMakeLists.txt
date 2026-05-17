cmake_minimum_required(VERSION 3.20)

project(MonomythClientDll LANGUAGES CXX)

add_library(dinput8 SHARED
    src/dllmain.cpp
    src/dinput_proxy.cpp
    src/logger.cpp
    src/fingerprint.cpp
    src/hook_manager.cpp
    src/opcode_reference.cpp
    src/packet_observer.cpp
    src/receive_dispatch_discovery.cpp
    src/runtime_capabilities.cpp
    src/server_auth_stats_observer.cpp
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

enable_testing()

add_executable(server_auth_stats_observer_tests
    tests/server_auth_stats_observer_tests.cpp
    src/logger.cpp
    src/opcode_reference.cpp
    src/server_auth_stats_observer.cpp
)
target_include_directories(server_auth_stats_observer_tests PRIVATE src)
target_compile_features(server_auth_stats_observer_tests PRIVATE cxx_std_20)
target_compile_definitions(server_auth_stats_observer_tests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
if(MSVC)
    set_property(TARGET server_auth_stats_observer_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

add_test(NAME server_auth_stats_observer_tests COMMAND server_auth_stats_observer_tests)
