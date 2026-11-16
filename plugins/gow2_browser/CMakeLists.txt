cmake_minimum_required(VERSION 3.16)

# The parser checks can also be built without the ImHex SDK.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(gow2_browser_checks LANGUAGES CXX)
    enable_testing()
    add_executable(gow2_browser_checks tests/parser_checks.cpp source/gow_format.cpp)
    target_include_directories(gow2_browser_checks PRIVATE include)
    target_compile_features(gow2_browser_checks PRIVATE cxx_std_23)
    add_test(NAME gow2_browser_checks COMMAND gow2_browser_checks)
    return()
endif()

include(ImHexPlugin)
add_imhex_plugin(
    NAME gow2_browser
    SOURCES
        source/plugin_gow2_browser.cpp
        source/gow_format.cpp
        source/asset_provider.cpp
        source/view_gow2_browser.cpp
    INCLUDES include
    LIBRARIES fonts
)

option(GOW2_BROWSER_BUILD_CHECKS "Build GoW2 browser parser and pattern checks" OFF)
if(GOW2_BROWSER_BUILD_CHECKS)
    add_executable(gow2_browser_checks tests/parser_checks.cpp source/gow_format.cpp)
    target_include_directories(gow2_browser_checks PRIVATE include)
    target_compile_features(gow2_browser_checks PRIVATE cxx_std_23)
    add_executable(gow2_pattern_checks tests/pattern_checks.cpp source/gow_format.cpp)
    target_include_directories(gow2_pattern_checks PRIVATE include)
    target_link_libraries(gow2_pattern_checks PRIVATE libpl libpl-gen fmt::fmt-header-only)
    target_compile_definitions(gow2_pattern_checks PRIVATE GOW2_PATTERN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/romfs/patterns")
    add_executable(gow2_integration_checks tests/integration_checks.cpp source/gow_format.cpp source/asset_provider.cpp source/view_gow2_browser.cpp)
    target_include_directories(gow2_integration_checks PRIVATE include)
    target_link_libraries(gow2_integration_checks PRIVATE libimhex fonts libromfs-gow2_browser imgui_all_includes libwolv fmt::fmt-header-only)
    target_compile_definitions(gow2_integration_checks PRIVATE IMHEX_PROJECT_NAME="gow2-integration-checks")
    addIncludesFromLibrary(gow2_integration_checks libpl)
    if(TARGET gow1_browser)
        add_executable(gow_browser_coexistence_checks tests/coexistence_checks.cpp)
        target_link_libraries(gow_browser_coexistence_checks PRIVATE libimhex fonts imgui_all_includes libwolv fmt::fmt-header-only)
        target_compile_definitions(gow_browser_coexistence_checks PRIVATE IMHEX_PROJECT_NAME="gow-browser-coexistence-checks")
        addIncludesFromLibrary(gow_browser_coexistence_checks libpl)
        add_dependencies(gow_browser_coexistence_checks gow1_browser gow2_browser)
    endif()
endif()
