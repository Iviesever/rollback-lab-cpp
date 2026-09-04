cmake_minimum_required(VERSION 3.28)

project(rollback_lab VERSION 0.2.0 LANGUAGES C CXX)

if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RollbackLabWarnings)

option(ROLLBACK_LAB_BUILD_TESTS "Build rollback-lab tests" ON)
option(ROLLBACK_LAB_BUILD_BENCHMARKS "Build rollback-lab benchmarks" ON)

add_library(rollback_lab STATIC
    src/version.cpp
    src/cli/commands.cpp
    src/core/hash.cpp
    src/netcode/session.cpp
    src/protocol/codec.cpp
    src/protocol/crc32.cpp
    src/protocol/sequence_window.cpp
    src/replay/replay.cpp
    src/report/canonical_json.cpp
    src/report/desync.cpp
    src/report/property_sweep.cpp
    src/report/scenario_runner.cpp
    src/report/trace.cpp
    src/report/viewer.cpp
    src/simulation/scripted_input.cpp
    src/simulation/simulation.cpp
    src/transport/process.cpp
    src/transport/seeded_transport.cpp
    src/transport/udp_socket.cpp
    src/udp/demo.cpp
    src/udp/peer.cpp
    src/udp/relay.cpp)
add_library(rollback_lab::rollback_lab ALIAS rollback_lab)
set_target_properties(rollback_lab PROPERTIES
    OUTPUT_NAME rollback_lab_core
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PDB_NAME rollback_lab_core
    COMPILE_PDB_NAME rollback_lab_core)
find_package(Git QUIET)
set(ROLLBACK_LAB_GIT_SHA "unavailable")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE ROLLBACK_LAB_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
target_compile_features(rollback_lab PUBLIC cxx_std_23)
target_include_directories(rollback_lab PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
rollback_lab_set_warnings(rollback_lab)
target_compile_definitions(rollback_lab PUBLIC
    ROLLBACK_LAB_GIT_SHA="${ROLLBACK_LAB_GIT_SHA}")
if(WIN32)
    target_link_libraries(rollback_lab PUBLIC ws2_32)
endif()

add_library(rollback_lab_c SHARED src/c_api/session_api.cpp src/c_api/live_api.cpp)
add_library(rollback_lab::rollback_lab_c ALIAS rollback_lab_c)
target_link_libraries(rollback_lab_c PRIVATE rollback_lab)
target_include_directories(rollback_lab_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(rollback_lab_c PRIVATE ROLLBACK_LAB_C_EXPORTS)
target_compile_features(rollback_lab_c INTERFACE c_std_11)
set_target_properties(rollback_lab_c PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION 1)
rollback_lab_set_warnings(rollback_lab_c)

add_executable(rollback_lab_cli src/cli/main.cpp)
set_target_properties(rollback_lab_cli PROPERTIES OUTPUT_NAME rollback_lab)
target_link_libraries(rollback_lab_cli PRIVATE rollback_lab)
rollback_lab_set_warnings(rollback_lab_cli)

add_executable(rollback_lab_c_demo tests/c_api/live_consumer.c)
set_target_properties(rollback_lab_c_demo PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(rollback_lab_c_demo PRIVATE rollback_lab_c)
if(MSVC)
    target_compile_options(rollback_lab_c_demo PRIVATE /W4 /WX /utf-8)
else()
    rollback_lab_set_warnings(rollback_lab_c_demo)
endif()

if(ROLLBACK_LAB_BUILD_TESTS AND BUILD_TESTING)
    add_executable(rollback_lab_c_consumer tests/c_api/c_consumer.c)
    set_target_properties(rollback_lab_c_consumer PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(rollback_lab_c_consumer PRIVATE rollback_lab_c)
    if(MSVC)
        target_compile_options(rollback_lab_c_consumer PRIVATE /W4 /WX /utf-8)
    else()
        rollback_lab_set_warnings(rollback_lab_c_consumer)
    endif()
    add_test(NAME c_abi_c11_consumer COMMAND rollback_lab_c_consumer)

    add_executable(rollback_lab_c_api_tests
        tests/test_main.cpp tests/c_api/session_api_test.cpp)
    target_include_directories(rollback_lab_c_api_tests PRIVATE tests)
    target_link_libraries(rollback_lab_c_api_tests PRIVATE rollback_lab_c rollback_lab)
    rollback_lab_set_warnings(rollback_lab_c_api_tests)
    add_test(NAME c_abi_session_tests COMMAND rollback_lab_c_api_tests)

    add_executable(rollback_lab_live_tests
        tests/test_main.cpp tests/c_api/live_test.cpp)
    target_include_directories(rollback_lab_live_tests PRIVATE tests)
    target_link_libraries(rollback_lab_live_tests PRIVATE rollback_lab_c rollback_lab)
    rollback_lab_set_warnings(rollback_lab_live_tests)
    add_test(NAME c_abi_live_tests COMMAND rollback_lab_live_tests)

    add_executable(rollback_lab_tests
        tests/test_main.cpp
        tests/test_smoke.cpp
        tests/unit/core_simulation_test.cpp
        tests/unit/rollback_session_test.cpp
        tests/protocol/protocol_transport_test.cpp
        tests/integration/replay_report_test.cpp
        tests/integration/cli_test.cpp
        tests/integration/viewer_generation_test.cpp
        tests/integration/live_scenario_test.cpp
        tests/udp/udp_demo_test.cpp)
    target_link_libraries(rollback_lab_tests PRIVATE rollback_lab)
    target_include_directories(rollback_lab_tests PRIVATE tests)
    rollback_lab_set_warnings(rollback_lab_tests)
    add_test(NAME rollback_lab_tests COMMAND rollback_lab_tests)

    add_executable(rollback_lab_protocol_fuzz_smoke
        tests/fuzz/protocol_fuzz_smoke.cpp)
    target_link_libraries(rollback_lab_protocol_fuzz_smoke PRIVATE rollback_lab)
    rollback_lab_set_warnings(rollback_lab_protocol_fuzz_smoke)
    add_test(NAME protocol_fuzz_smoke COMMAND rollback_lab_protocol_fuzz_smoke)

    add_executable(rollback_lab_property_sweep
        tests/property/property_sweep.cpp)
    target_link_libraries(rollback_lab_property_sweep PRIVATE rollback_lab)
    rollback_lab_set_warnings(rollback_lab_property_sweep)
    add_test(NAME property_sweep_smoke
        COMMAND rollback_lab_property_sweep --seeds 100 --repeat-samples 16)
    set_tests_properties(c_abi_c11_consumer c_abi_session_tests c_abi_live_tests
        rollback_lab_tests protocol_fuzz_smoke property_sweep_smoke PROPERTIES TIMEOUT 300)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS rollback_lab rollback_lab_c rollback_lab_cli rollback_lab_c_demo
    EXPORT rollback_labTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE README.md DESTINATION .)
install(FILES cmake/SDK_README.md DESTINATION . RENAME README-SDK.md)
configure_package_config_file(cmake/rollback_labConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rollback_labConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rollback_lab)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/rollback_labConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT rollback_labTargets NAMESPACE rollback_lab::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rollback_lab)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/rollback_labConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/rollback_labConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rollback_lab)
