cmake_minimum_required(VERSION 3.22)

# ---- Plug-in identity -------------------------------------------------------
# Changing the codes after release will break saved host sessions.
set(EF_COMPANY_NAME      "ZBAudio")
set(EF_PRODUCT_NAME      "Echo Factory")
set(EF_BUNDLE_ID         "com.zbaudio.echofactory")
set(EF_MANUFACTURER_CODE "Zbau")   # 4 chars, at least one upper-case
set(EF_PLUGIN_CODE       "Ecfy")   # 4 chars, exactly one upper-case

project(ECHO_FACTORY VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

set(EF_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EF_PLUGIN_FORMATS AU)
endif()
option(EF_COPY_PLUGIN_AFTER_BUILD "Install plugins after building" ${APPLE})

add_subdirectory(JUCE)

juce_add_plugin(EchoFactory
    COMPANY_NAME             "${EF_COMPANY_NAME}"
    BUNDLE_ID                "${EF_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE ${EF_MANUFACTURER_CODE}
    PLUGIN_CODE              ${EF_PLUGIN_CODE}
    PRODUCT_NAME             "${EF_PRODUCT_NAME}"
    FORMATS                  ${EF_PLUGIN_FORMATS}
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE   # AU stays aufx (AU_MAIN_TYPE), so sessions keep loading
    NEEDS_MIDI_OUTPUT        FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    IS_MIDI_EFFECT           FALSE
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Delay
    MICROPHONE_PERMISSION_ENABLED TRUE   # without it macOS gives the Standalone silent input
    MICROPHONE_PERMISSION_TEXT    "Echo Factory processes live audio from your microphone or audio interface."
    COPY_PLUGIN_AFTER_BUILD  ${EF_COPY_PLUGIN_AFTER_BUILD}
    NEEDS_WEB_BROWSER        FALSE
    NEEDS_CURL               FALSE)

# Windows needs an explicit initial focus handoff after showing the native window.
# Keep the plugin wrapper and other platforms on their existing entry points.
if(WIN32)
    target_sources(EchoFactory_Standalone PRIVATE Source/StandaloneApp.cpp)
    target_compile_definitions(EchoFactory_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

target_sources(EchoFactory
    PRIVATE
        Source/Parameters.cpp
        Source/DelayEngine.cpp
        Source/ReadHead.cpp
        Source/Taps.cpp
        Source/PresetManager.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ui/PerformPad.cpp
        Source/ui/PresetBar.cpp
        Source/ui/SavePresetDialog.cpp
        Source/ui/TapsPanel.cpp)

target_include_directories(EchoFactory PRIVATE Source)

target_compile_definitions(EchoFactory
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(EchoFactory
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline engine tests (not part of the default build) ------------------
juce_add_console_app(EditorFocusTests PRODUCT_NAME "EditorFocusTests")
set_target_properties(EditorFocusTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(EditorFocusTests PRIVATE tests/EditorFocusTests.cpp)
target_include_directories(EditorFocusTests PRIVATE Source)
target_link_libraries(EditorFocusTests PRIVATE EchoFactory juce::juce_audio_utils juce::juce_dsp)

#   cmake --build build --target EngineTests && ./build/EngineTests_artefacts/Debug/EngineTests
juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
set_target_properties(EngineTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(EngineTests PRIVATE tests/EngineTests.cpp Source/DelayEngine.cpp Source/ReadHead.cpp)
target_include_directories(EngineTests PRIVATE Source)
target_compile_definitions(EngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(EngineTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
