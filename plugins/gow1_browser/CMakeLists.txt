cmake_minimum_required(VERSION 3.16)

# The parser checks can also be built without the ImHex SDK.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(gow1_browser_checks LANGUAGES CXX)
    enable_testing()
    add_executable(gow1_browser_checks tests/parser_checks.cpp source/gow_format.cpp)
    target_include_directories(gow1_browser_checks PRIVATE include)
    target_compile_features(gow1_browser_checks PRIVATE cxx_std_23)
    add_test(NAME gow1_browser_checks COMMAND gow1_browser_checks)
    return()
endif()

include(ImHexPlugin)
add_imhex_plugin(
    NAME gow1_browser
    SOURCES
        source/plugin_gow1_browser.cpp
        source/gow_format.cpp
        source/asset_provider.cpp
        source/view_gow1_browser.cpp
    INCLUDES include
    LIBRARIES fonts
)

option(GOW1_BROWSER_BUILD_CHECKS "Build GoW1 browser parser and pattern checks" OFF)
if(GOW1_BROWSER_BUILD_CHECKS)
    add_executable(gow1_browser_checks tests/parser_checks.cpp source/gow_format.cpp)
    target_include_directories(gow1_browser_checks PRIVATE include)
    target_compile_features(gow1_browser_checks PRIVATE cxx_std_23)
    add_executable(gow1_pattern_checks tests/pattern_checks.cpp source/gow_format.cpp)
    target_include_directories(gow1_pattern_checks PRIVATE include)
    target_link_libraries(gow1_pattern_checks PRIVATE libpl libpl-gen fmt::fmt-header-only)
    target_compile_definitions(gow1_pattern_checks PRIVATE GOW1_PATTERN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/romfs/patterns")
    add_executable(gow1_integration_checks tests/integration_checks.cpp source/gow_format.cpp source/asset_provider.cpp source/view_gow1_browser.cpp)
    target_include_directories(gow1_integration_checks PRIVATE include)
    target_link_libraries(gow1_integration_checks PRIVATE libimhex fonts libromfs-gow1_browser imgui_all_includes libwolv fmt::fmt-header-only)
    target_compile_definitions(gow1_integration_checks PRIVATE IMHEX_PROJECT_NAME="gow1-integration-checks")
    addIncludesFromLibrary(gow1_integration_checks libpl)
endif()
