cmake_minimum_required(VERSION 3.22)
project(LightHost VERSION 1.2.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Override with -DJUCE_DIR=<path to JUCE> if JUCE lives elsewhere
if(NOT DEFINED JUCE_DIR)
    foreach(candidate "${CMAKE_SOURCE_DIR}/LightHost/lib/juice" "${CMAKE_SOURCE_DIR}/lib/juce" "${CMAKE_SOURCE_DIR}/lib/juice")
        if(EXISTS "${candidate}/CMakeLists.txt")
            set(JUCE_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT JUCE_DIR)
    message(FATAL_ERROR "JUCE not found. Pass -DJUCE_DIR=<path to JUCE>")
endif()

add_subdirectory("${JUCE_DIR}" juce EXCLUDE_FROM_ALL)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIGHTHOST_CURL REQUIRED libcurl)
endif()

set(VST2_SDK_DIR "${CMAKE_SOURCE_DIR}/VST2Compat" CACHE PATH
    "Directory containing JUCE-compatible VST2 interface headers")
if(NOT VST2_SDK_DIR)
    set(VST2_SDK_DIR "${CMAKE_SOURCE_DIR}/VST2Compat")
endif()
if(NOT EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h"
   OR NOT EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffectx.h")
    message(FATAL_ERROR "VST2 support is required: VST2_SDK_DIR must contain pluginterfaces/vst2.x/aeffect.h and aeffectx.h")
endif()

juce_add_gui_app(LightHost
    PRODUCT_NAME "Light Host"
    COMPANY_NAME "Rolando Islas"
    BUNDLE_ID com.rolandoislas.lighthost
    NEEDS_CURL TRUE
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/icon.png")

juce_add_binary_data(LightHostData SOURCES
    Resources/icon.png
    Resources/menu_icon.png
    Resources/menu_icon_white.png)

juce_generate_juce_header(LightHost)

target_sources(LightHost PRIVATE
    Source/ErrorReporting.hpp
    Source/GlobalBypassShortcut.cpp
    Source/GlobalBypassShortcut.hpp
    Source/HostStartup.cpp
    Source/IconMenu.cpp
    Source/PluginChannelRouting.hpp
    Source/StereoRouting.hpp
    Source/UpdateManifestValidation.hpp
    Source/PluginWindow.cpp)

target_compile_definitions(LightHost PRIVATE
    JUCE_PLUGINHOST_VST=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_CATCH_UNHANDLED_EXCEPTIONS=1
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

if(WIN32)
    target_compile_definitions(LightHost PRIVATE JUCE_ASIO=1)
endif()

option(LIGHTHOST_BUILD_TESTS "Build Light Host regression tests" OFF)
if(LIGHTHOST_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(LightHostRoutingTest PRODUCT_NAME "Light Host Routing Test")
    juce_generate_juce_header(LightHostRoutingTest)
    target_sources(LightHostRoutingTest PRIVATE Tests/StereoRoutingTest.cpp)
    target_compile_definitions(LightHostRoutingTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(LightHostRoutingTest PRIVATE juce::juce_audio_utils)
    add_test(NAME LightHostRoutingTest COMMAND LightHostRoutingTest)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        juce_add_console_app(LightHostHttpsSmoke PRODUCT_NAME "Light Host HTTPS Smoke Test" NEEDS_CURL TRUE)
        target_sources(LightHostHttpsSmoke PRIVATE Tests/HttpsSmokeTest.cpp)
        target_link_libraries(LightHostHttpsSmoke PRIVATE juce::juce_core)
    endif()
endif()
if(APPLE)
    target_compile_definitions(LightHost PRIVATE JUCE_PLUGINHOST_AU=1)
    target_link_libraries(LightHost PRIVATE "-framework Carbon")
    add_executable(LightHostUpdater Installer/UpdateHelper.cpp)
elseif(UNIX)
    target_compile_definitions(LightHost PRIVATE JUCE_PLUGINHOST_LV2=1)
    find_package(X11 REQUIRED)
    target_link_libraries(LightHost PRIVATE X11::X11)
    add_executable(LightHostUpdater Installer/UpdateHelper.cpp)
    juce_add_gui_app(LightHostSetup PRODUCT_NAME "Light Host Setup" NEEDS_CURL TRUE)
    target_sources(LightHostSetup PRIVATE Installer/LinuxInstaller.cpp)
    target_compile_definitions(LightHostSetup PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0)
    target_link_libraries(LightHostSetup PRIVATE juce::juce_gui_basics juce::juce_cryptography)
endif()
if(UNIX)
    add_dependencies(LightHost LightHostUpdater)
    add_custom_command(TARGET LightHost POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:LightHostUpdater>" "$<TARGET_FILE_DIR:LightHost>/LightHostUpdater"
        VERBATIM)
endif()

target_include_directories(LightHost PRIVATE "${VST2_SDK_DIR}")

target_link_libraries(LightHost PRIVATE
    LightHostData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if(WIN32)
    add_executable(LightHostSetup WIN32 Installer/WindowsInstaller.cpp)
    target_compile_definitions(LightHostSetup PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(LightHostSetup PRIVATE shell32 ole32 uuid
        juce::juce_core juce::juce_cryptography)
    set_target_properties(LightHostSetup PROPERTIES OUTPUT_NAME "Light Host Setup")
endif()
