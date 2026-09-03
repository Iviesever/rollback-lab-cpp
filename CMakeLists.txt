cmake_minimum_required(VERSION 3.28)

project(rollback_lab VERSION 0.1.0 LANGUAGES CXX)

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

add_executable(rollback_lab_cli src/cli/main.cpp)
set_target_properties(rollback_lab_cli PROPERTIES OUTPUT_NAME rollback_lab)
target_link_libraries(rollback_lab_cli PRIVATE rollback_lab)
rollback_lab_set_warnings(rollback_lab_cli)

if(ROLLBACK_LAB_BUILD_TESTS AND BUILD_TESTING)
    add_executable(rollback_lab_tests
        tests/test_main.cpp
        tests/test_smoke.cpp
        tests/unit/core_simulation_test.cpp
        tests/unit/rollback_session_test.cpp
        tests/protocol/protocol_transport_test.cpp
        tests/integration/replay_report_test.cpp
        tests/integration/cli_test.cpp
        tests/integration/viewer_generation_test.cpp
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
endif()
