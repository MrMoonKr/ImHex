cmake_minimum_required(VERSION 3.16)

include(ImHexPlugin)
add_imhex_plugin(
    NAME gow3_browser
    SOURCES source/plugin_gow3_browser.cpp source/gow_format.cpp source/asset_provider.cpp source/view_gow3_browser.cpp
    INCLUDES include
    LIBRARIES fonts
)

option(GOW3_BROWSER_BUILD_CHECKS "Build GoW III Remastered browser parser, pattern and integration checks" OFF)
if(GOW3_BROWSER_BUILD_CHECKS)
    add_executable(gow3_browser_checks tests/parser_checks.cpp source/gow_format.cpp)
    target_include_directories(gow3_browser_checks PRIVATE include)
    add_executable(gow3_pattern_checks tests/pattern_checks.cpp source/gow_format.cpp)
    target_include_directories(gow3_pattern_checks PRIVATE include)
    target_link_libraries(gow3_pattern_checks PRIVATE libpl libpl-gen fmt::fmt-header-only)
    target_compile_definitions(gow3_pattern_checks PRIVATE GOW3_PATTERN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/romfs/patterns")
    add_executable(gow3_integration_checks tests/integration_checks.cpp source/gow_format.cpp source/asset_provider.cpp source/view_gow3_browser.cpp)
    target_include_directories(gow3_integration_checks PRIVATE include)
    target_link_libraries(gow3_integration_checks PRIVATE libimhex libpl fonts libromfs-gow3_browser imgui_all_includes libwolv fmt::fmt-header-only)
    target_compile_definitions(gow3_integration_checks PRIVATE IMHEX_PROJECT_NAME="gow3-integration-checks")
    addIncludesFromLibrary(gow3_integration_checks libpl)
    if(TARGET gow1_browser AND TARGET gow2_browser AND "gowr_browser" IN_LIST PLUGINS)
        add_executable(gow3_coexistence_checks tests/coexistence_checks.cpp)
        target_link_libraries(gow3_coexistence_checks PRIVATE libimhex fonts imgui_all_includes libwolv fmt::fmt-header-only)
        target_compile_definitions(gow3_coexistence_checks PRIVATE IMHEX_PROJECT_NAME="gow3-coexistence-checks")
        addIncludesFromLibrary(gow3_coexistence_checks libpl)
        add_dependencies(gow3_coexistence_checks gow1_browser gow2_browser gow3_browser gowr_browser)
    endif()
endif()
