cmake_minimum_required(VERSION 3.22)
project(BoomBap VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE (fetched automatically; pin the tag you want) ----
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- standalone app ----
juce_add_gui_app(BoomBap
    PRODUCT_NAME "BoomBap"
    VERSION 0.1.0
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "BoomBap uses the microphone for the vocal channel.")

target_sources(BoomBap PRIVATE
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    Source/App/MainComponentMedia.cpp
    Source/App/MainComponentProject.cpp
    Source/App/MainComponent.h
    Source/App/UiComponents.cpp
    Source/App/UiComponents.h
    Source/App/WavWrite.h
    Source/App/BoomBapLookAndFeel.h
    Source/App/PadBank.h
    Source/App/PianoRoll.h
    Source/App/Playlist.h
    Source/App/Multitrack.h
    Source/App/SequencerPanel.h
    Source/App/ProjectFile.cpp
    Source/App/ProjectFile.h
    Source/App/Engine/BoomBapEngine.h)

target_compile_definitions(BoomBap PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BoomBap,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BoomBap,JUCE_VERSION>")

target_link_libraries(BoomBap PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- plugin (VST3 on every desktop platform; AU joins in on macOS) ----
juce_add_plugin(BoomBapPlugin
    PRODUCT_NAME "BoomBap"
    VERSION 0.1.0
    COMPANY_NAME "Lawrence Legacy Press"
    BUNDLE_ID "com.lawrencelegacypress.boombap"
    PLUGIN_MANUFACTURER_CODE "LaLe"
    PLUGIN_CODE "Bmbp"
    FORMATS VST3 AU
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Instrument" "Synth"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "BoomBap uses the microphone for the vocal channel.")

target_sources(BoomBapPlugin PRIVATE
    Source/App/PluginProcessor.cpp
    Source/App/PluginProcessor.h
    Source/App/PluginEditor.cpp
    Source/App/PluginEditor.h
    Source/App/MainComponent.cpp
    Source/App/MainComponentMedia.cpp
    Source/App/MainComponentProject.cpp
    Source/App/MainComponent.h
    Source/App/UiComponents.cpp
    Source/App/UiComponents.h
    Source/App/WavWrite.h
    Source/App/BoomBapLookAndFeel.h
    Source/App/PadBank.h
    Source/App/PianoRoll.h
    Source/App/Playlist.h
    Source/App/Multitrack.h
    Source/App/SequencerPanel.h
    Source/App/ProjectFile.cpp
    Source/App/ProjectFile.h
    Source/App/Engine/BoomBapEngine.h)

target_compile_definitions(BoomBapPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BoomBapPlugin PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- headless DSP/engine test (no JUCE needed) ----
option(BOOMBAP_BUILD_TESTS "Build the headless engine test" ON)
if(BOOMBAP_BUILD_TESTS)
    add_executable(EngineTest Tests/EngineTest.cpp)
    target_compile_features(EngineTest PRIVATE cxx_std_17)
    enable_testing()
    add_test(NAME engine COMMAND EngineTest)

    # project file format test (needs only juce_core)
    add_executable(ProjectFileTest Tests/ProjectFileTest.cpp Source/App/ProjectFile.cpp)
    target_link_libraries(ProjectFileTest PRIVATE juce::juce_core)
    target_compile_definitions(ProjectFileTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    add_test(NAME project_file COMMAND ProjectFileTest)

    # plugin host-harness test (loads the built VST3 and plays it)
    add_executable(PluginLoadTest Tests/PluginLoadTest.cpp)
    target_link_libraries(PluginLoadTest PRIVATE juce::juce_audio_processors juce::juce_gui_basics)
    if(UNIX AND NOT APPLE)
        target_link_libraries(PluginLoadTest PRIVATE X11)
    endif()
    target_compile_definitions(PluginLoadTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
    if(TARGET BoomBapPlugin_VST3)
        add_dependencies(PluginLoadTest BoomBapPlugin_VST3)
    endif()
    add_test(NAME plugin_load COMMAND PluginLoadTest
             "${CMAKE_BINARY_DIR}/BoomBapPlugin_artefacts/$<CONFIG>/VST3/BoomBap.vst3")

    # editor lifecycle test (processor + editor from source, no VST3 layer)
    add_executable(EditorLifecycleTest
        Tests/EditorLifecycleTest.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(EditorLifecycleTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(EditorLifecycleTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(EditorLifecycleTest PRIVATE X11)
    endif()
    add_test(NAME editor_lifecycle COMMAND EditorLifecycleTest)

    # plugin render test (items 59/61/64: first-open view state, restored
    # take homing, and measured Glue/Dust/mic verdicts through processBlock)
    add_executable(PluginRenderTest
        Tests/PluginRenderTest.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(PluginRenderTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(PluginRenderTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(PluginRenderTest PRIVATE X11)
    endif()
    add_test(NAME plugin_render COMMAND PluginRenderTest)

    # recent-projects list test (storage logic, no UI)
    add_executable(RecentProjectsTest Tests/RecentProjectsTest.cpp)
    target_link_libraries(RecentProjectsTest PRIVATE juce::juce_gui_extra juce::juce_gui_basics)
    target_compile_definitions(RecentProjectsTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    add_test(NAME recent_files COMMAND RecentProjectsTest)
endif()
