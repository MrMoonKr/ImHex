cmake_minimum_required(VERSION 3.16)

find_package(lz4 CONFIG REQUIRED)
include(ImHexPlugin)
add_imhex_plugin(
    NAME gowr_browser
    SOURCES source/plugin_gowr_browser.cpp source/gow_format.cpp source/asset_provider.cpp source/view_gowr_browser.cpp
    INCLUDES include
    LIBRARIES fonts lz4::lz4
)

option(GOWR_BROWSER_BUILD_CHECKS "Build Ragnarok browser parser, pattern and integration checks" OFF)
if(GOWR_BROWSER_BUILD_CHECKS)
    add_executable(gowr_browser_checks tests/parser_checks.cpp source/gow_format.cpp)
    target_include_directories(gowr_browser_checks PRIVATE include)
    target_link_libraries(gowr_browser_checks PRIVATE lz4::lz4)
    add_executable(gowr_pattern_checks tests/pattern_checks.cpp source/gow_format.cpp)
    target_include_directories(gowr_pattern_checks PRIVATE include)
    target_link_libraries(gowr_pattern_checks PRIVATE libpl libpl-gen fmt::fmt-header-only lz4::lz4)
    target_compile_definitions(gowr_pattern_checks PRIVATE GOWR_PATTERN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/romfs/patterns")
    add_executable(gowr_integration_checks tests/integration_checks.cpp source/gow_format.cpp source/asset_provider.cpp source/view_gowr_browser.cpp)
    target_include_directories(gowr_integration_checks PRIVATE include)
    target_link_libraries(gowr_integration_checks PRIVATE libimhex fonts libromfs-gowr_browser imgui_all_includes libwolv fmt::fmt-header-only lz4::lz4)
    target_compile_definitions(gowr_integration_checks PRIVATE IMHEX_PROJECT_NAME="gowr-integration-checks")
    addIncludesFromLibrary(gowr_integration_checks libpl)
    if(TARGET gow1_browser AND TARGET gow2_browser)
        add_executable(gow_all_browser_checks tests/coexistence_checks.cpp)
        target_link_libraries(gow_all_browser_checks PRIVATE libimhex fonts imgui_all_includes libwolv fmt::fmt-header-only)
        target_compile_definitions(gow_all_browser_checks PRIVATE IMHEX_PROJECT_NAME="gow-all-browser-checks")
        addIncludesFromLibrary(gow_all_browser_checks libpl)
        add_dependencies(gow_all_browser_checks gow1_browser gow2_browser gowr_browser)
    endif()
endif()
