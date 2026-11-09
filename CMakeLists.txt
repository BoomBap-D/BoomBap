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
    Source/App/StationPanel.h
    Source/App/UndoHistory.h
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
    Source/App/StationPanel.h
    Source/App/UndoHistory.h
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

    # undo/redo test (Pass 2 item 38: step/fader/clip/strip actions undo
    # and redo to exact prior state; transport never enters the history)
    add_executable(UndoTest
        Tests/UndoTest.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(UndoTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(UndoTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(UndoTest PRIVATE X11)
    endif()
    add_test(NAME undo COMMAND UndoTest)

    # dock-back regression test (item 66: pop-out -> dock-back cycles for
    # all three production views, both dock paths, plus shutdown-while-
    # popped; the tab system lives in MainComponent, hosted here by the
    # plugin editor exactly as in EditorLifecycleTest)
    add_executable(DockBackTest
        Tests/DockBackTest.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(DockBackTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(DockBackTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(DockBackTest PRIVATE X11)
    endif()
    add_test(NAME dock_back COMMAND DockBackTest)

    # window-frame test (Fix Pass 4, items 77/78: every window type
    # wears the app-drawn dark title bar and the auxiliary windows
    # float above the main window; includes the item-66 pop-out /
    # dock-back battery re-run under the new frame path)
    add_executable(WindowFramesTest
        Tests/WindowFramesTest.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(WindowFramesTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(WindowFramesTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(WindowFramesTest PRIVATE X11)
    endif()
    add_test(NAME window_frames COMMAND WindowFramesTest)

    # micro-fix regression test (items 67-68: the docked multitrack's
    # lane scrollbar must actually scroll - its range model pinned the
    # start at 0 - and the mixer-order chips must render PLAYING as an
    # outline box and SELECTED as a different, solid light; the
    # component is header-only over the pure-C++ engine, hosted on the
    # desktop exactly as the app hosts it)
    add_executable(MicroFixTest Tests/MicroFixTest.cpp)
    target_link_libraries(MicroFixTest PRIVATE juce::juce_gui_basics)
    target_compile_definitions(MicroFixTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(UNIX AND NOT APPLE)
        target_link_libraries(MicroFixTest PRIVATE X11)
    endif()
    add_test(NAME micro_fix COMMAND MicroFixTest)

    # pad-load regression test (Fix Pass 3 item 75: every ordinary WAV
    # variant - PCM depths, float, WAVE_FORMAT_EXTENSIBLE, BWF, header
    # clutter, uppercase name - must travel the real pad load path and
    # land on the pad; D's pad bank "couldn't load a wav")
    add_executable(PadLoadTest
        Tests/PadLoadTest.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(PadLoadTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(PadLoadTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(PadLoadTest PRIVATE X11)
    endif()
    add_test(NAME pad_load COMMAND PadLoadTest)

    # fix-pass-3 regression test (items 69/71/72/74/76: track deletion
    # and marquee clip delete as single undoable actions with no ghost
    # clips, the step-sequencer swipe grammar driven with real mouse
    # events, the pad bank's click-to-load waveform, rotary knobs and
    # info strip)
    add_executable(FixPass3Test
        Tests/FixPass3Test.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(FixPass3Test PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(FixPass3Test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(FixPass3Test PRIVATE X11)
    endif()
    add_test(NAME fix_pass3 COMMAND FixPass3Test)

    # round-1 regression test (Breakdown #1 items 86/87/89: a sequencer
    # name click SELECTS and only selects - the menu lives on
    # right-click - with the one selection state mirrored into the
    # multitrack chip and the Station binding; a visible delete X on
    # user-added sequencer rows running item 69's guarded undoable
    # delete; the waveform panel auditions on click when loaded and
    # its button is LOAD, with the amber action trace on the action
    # buttons. Items 85/102 live in WindowFramesTest.)
    add_executable(Round1Test
        Tests/Round1Test.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(Round1Test PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(Round1Test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(Round1Test PRIVATE X11)
    endif()
    add_test(NAME round1 COMMAND Round1Test)

    # round-2 regression test (Breakdown #1 items 93/94/95/96 + the
    # Station rework: transport park/seek + double-press skips, the
    # three clock faces and their persistence, the channel drop-down,
    # the uniform chain, EQ knob<->node sync, board mute/solo as one
    # engine state with the audio path to prove it, smoothed fader
    # gains, the console fader's cap, factory presets, the master VU)
    add_executable(Round2Test
        Tests/Round2Test.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(Round2Test PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(Round2Test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(Round2Test PRIVATE X11)
    endif()
    add_test(NAME round2 COMMAND Round2Test)

    # round-3 regression test (Breakdown #1 items 88/100/101/80: track
    # naming in the first column only with LOAD never renaming itself,
    # the pad name strip, pad captions back + drag copy semantics,
    # Start/End back to sliders two-way synced with the pull bars, and
    # the multi-point envelope - model, settings/file round trips, and
    # the measured effect on the rendered sound)
    add_executable(Round3Test
        Tests/Round3Test.cpp
        Source/App/PluginProcessor.cpp
        Source/App/PluginEditor.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponentMedia.cpp
        Source/App/MainComponentProject.cpp
        Source/App/UiComponents.cpp
        Source/App/ProjectFile.cpp)
    target_link_libraries(Round3Test PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_gui_extra)
    target_compile_definitions(Round3Test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_link_libraries(Round3Test PRIVATE X11)
    endif()
    add_test(NAME round3 COMMAND Round3Test)

    # recent-projects list test (storage logic, no UI)
    add_executable(RecentProjectsTest Tests/RecentProjectsTest.cpp)
    target_link_libraries(RecentProjectsTest PRIVATE juce::juce_gui_extra juce::juce_gui_basics)
    target_compile_definitions(RecentProjectsTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    add_test(NAME recent_files COMMAND RecentProjectsTest)
endif()
