cmake_minimum_required(VERSION 3.20)

project(MonomythClientDll LANGUAGES CXX)

function(monomyth_apply_mingw_static_runtime target_name)
    if(MINGW)
        target_link_options(${target_name} PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
endfunction()

add_library(dinput8 SHARED
    src/dllmain.cpp
    src/dinput_proxy.cpp
    src/class_display_discovery.cpp
    src/logger.cpp
    src/fingerprint.cpp
    src/hook_manager.cpp
    src/multiclass_cache.cpp
    src/multiclass_identity.cpp
    src/multiclass_skill_visibility.cpp
    src/opcode_reference.cpp
    src/packet_observer.cpp
    src/receive_dispatch_discovery.cpp
    src/runtime_capabilities.cpp
    src/server_auth_stats_observer.cpp
    src/spell_level_selection.cpp
    src/spell_usability_discovery_policy.cpp
    src/spell_usability_discovery.cpp
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
monomyth_apply_mingw_static_runtime(dinput8)

enable_testing()

add_executable(server_auth_stats_observer_tests
    tests/server_auth_stats_observer_tests.cpp
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
monomyth_apply_mingw_static_runtime(server_auth_stats_observer_tests)

add_test(NAME server_auth_stats_observer_tests COMMAND server_auth_stats_observer_tests)

add_executable(spell_level_selection_tests
    tests/spell_level_selection_tests.cpp
    src/multiclass_identity.cpp
    src/spell_level_selection.cpp
)
target_include_directories(spell_level_selection_tests PRIVATE src)
target_compile_features(spell_level_selection_tests PRIVATE cxx_std_20)
monomyth_apply_mingw_static_runtime(spell_level_selection_tests)

add_test(NAME spell_level_selection_tests COMMAND spell_level_selection_tests)

add_executable(multiclass_identity_tests
    tests/multiclass_identity_tests.cpp
    src/multiclass_identity.cpp
)
target_include_directories(multiclass_identity_tests PRIVATE src)
target_compile_features(multiclass_identity_tests PRIVATE cxx_std_20)
monomyth_apply_mingw_static_runtime(multiclass_identity_tests)

add_test(NAME multiclass_identity_tests COMMAND multiclass_identity_tests)

add_executable(multiclass_skill_visibility_tests
    tests/multiclass_skill_visibility_tests.cpp
    src/multiclass_skill_visibility.cpp
)
target_include_directories(multiclass_skill_visibility_tests PRIVATE src)
target_compile_features(multiclass_skill_visibility_tests PRIVATE cxx_std_20)
monomyth_apply_mingw_static_runtime(multiclass_skill_visibility_tests)

add_test(NAME multiclass_skill_visibility_tests COMMAND multiclass_skill_visibility_tests)

add_executable(class_display_discovery_tests
    tests/class_display_discovery_tests.cpp
    src/class_display_discovery.cpp
    src/spell_usability_discovery_policy.cpp
)
target_include_directories(class_display_discovery_tests PRIVATE src)
target_compile_features(class_display_discovery_tests PRIVATE cxx_std_20)
monomyth_apply_mingw_static_runtime(class_display_discovery_tests)

add_test(NAME class_display_discovery_tests COMMAND class_display_discovery_tests)

add_executable(runtime_capabilities_tests
    tests/runtime_capabilities_tests.cpp
    src/runtime_capabilities.cpp
    src/spell_usability_discovery_policy.cpp
)
target_include_directories(runtime_capabilities_tests PRIVATE src)
target_compile_features(runtime_capabilities_tests PRIVATE cxx_std_20)
monomyth_apply_mingw_static_runtime(runtime_capabilities_tests)

add_test(NAME runtime_capabilities_tests COMMAND runtime_capabilities_tests)

add_executable(spell_usability_discovery_policy_tests
    tests/spell_usability_discovery_policy_tests.cpp
    src/spell_usability_discovery_policy.cpp
)
target_include_directories(spell_usability_discovery_policy_tests PRIVATE src)
target_compile_features(spell_usability_discovery_policy_tests PRIVATE cxx_std_20)
monomyth_apply_mingw_static_runtime(spell_usability_discovery_policy_tests)

add_test(
    NAME spell_usability_discovery_policy_tests
    COMMAND spell_usability_discovery_policy_tests
)
