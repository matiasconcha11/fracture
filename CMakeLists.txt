cmake_minimum_required(VERSION 3.22)

# Must be set before project(). Builds are native-only by default; release builds pass
# -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal binary.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Fracture VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Fracture
    COMPANY_NAME "Matias Concha"
    BUNDLE_ID com.matiasconcha.fracture
    PLUGIN_MANUFACTURER_CODE Mcon
    PLUGIN_CODE Frct
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Fracture"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

set(FRACTURE_SOURCES
    Source/ChoiceSelector.cpp
    Source/Engine.cpp
    Source/LookAndFeel.cpp
    Source/Patterns.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp)

target_sources(Fracture PRIVATE ${FRACTURE_SOURCES})
target_compile_definitions(Fracture PRIVATE FRACTURE_VERSION="${PROJECT_VERSION}")

target_compile_definitions(Fracture PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Fracture
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(FRACTURE_BUILD_TESTS "Build the unit tests" ON)

if(FRACTURE_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    # The tests compile the same sources as the plugin, without a plugin wrapper around them.
    add_executable(FractureTests
        ${FRACTURE_SOURCES}
        Tests/FractureTests.cpp)

    target_link_libraries(FractureTests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain)

    target_include_directories(FractureTests PRIVATE Source)
    target_compile_definitions(FractureTests PRIVATE
        FRACTURE_VERSION="${PROJECT_VERSION}"
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    include(CTest)
    include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
    catch_discover_tests(FractureTests)
endif()
