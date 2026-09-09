cmake_minimum_required(VERSION 3.20)
project(er_calculator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(er_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /fp:strict)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off)
    endif()
endfunction()

add_library(er_combat STATIC combat/src/calculator.cpp)
target_include_directories(er_combat PUBLIC combat/include)
er_configure_target(er_combat)

add_library(er_scenario_io STATIC cli/src/scenario_io.cpp)
target_include_directories(er_scenario_io PUBLIC cli/include)
target_link_libraries(er_scenario_io PUBLIC er_combat)
er_configure_target(er_scenario_io)

add_library(er_catalog STATIC catalog/src/catalog.cpp)
target_include_directories(er_catalog PUBLIC catalog/include)
er_configure_target(er_catalog)

add_library(er_loadout STATIC catalog/src/loadout.cpp)
target_link_libraries(er_loadout PUBLIC er_catalog er_combat)
er_configure_target(er_loadout)

add_library(er_named_cli STATIC cli/src/named_cli.cpp)
target_include_directories(er_named_cli PUBLIC cli/include)
target_link_libraries(er_named_cli PUBLIC er_loadout er_scenario_io)
er_configure_target(er_named_cli)

add_executable(er_calc cli/src/main.cpp)
target_link_libraries(er_calc PRIVATE er_named_cli)
if(WIN32 AND NOT MSVC)
    target_link_options(er_calc PRIVATE -municode)
endif()
er_configure_target(er_calc)

include(CTest)
if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE er_combat)
    er_configure_target(core_tests)
    add_test(NAME core COMMAND core_tests)

    add_executable(io_tests tests/io_tests.cpp)
    target_link_libraries(io_tests PRIVATE er_scenario_io)
    er_configure_target(io_tests)
    add_test(NAME io COMMAND io_tests)

    add_executable(catalog_tests tests/catalog_tests.cpp)
    target_link_libraries(catalog_tests PRIVATE er_catalog)
    er_configure_target(catalog_tests)
    add_test(NAME catalog COMMAND catalog_tests)

    add_executable(loadout_tests tests/loadout_tests.cpp)
    target_link_libraries(loadout_tests PRIVATE er_loadout)
    er_configure_target(loadout_tests)
    add_test(NAME loadout COMMAND loadout_tests)

    add_executable(named_cli_tests tests/named_cli_tests.cpp)
    target_link_libraries(named_cli_tests PRIVATE er_named_cli)
    er_configure_target(named_cli_tests)
    add_test(NAME named_cli COMMAND named_cli_tests)

    add_test(NAME cli_smoke COMMAND er_calc
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/basic_attack.ini" --format json)
endif()
