cmake_minimum_required(VERSION 3.22)

# App Store Connect refuses an upload whose bundle version it has already
# seen, so the version has to move every build. JUCE writes
# CFBundleVersion into the Info.plist from this project version, which
# means it must be set HERE - an xcodebuild setting like
# CURRENT_PROJECT_VERSION arrives too late, because the plist JUCE
# generates contains a literal rather than a reference to it.
if (DEFINED ENV{BUILD_NUMBER} AND NOT "$ENV{BUILD_NUMBER}" STREQUAL "")
    set(ES_BUILD "$ENV{BUILD_NUMBER}")
else()
    set(ES_BUILD "0")
endif()

project(ExpressionSynth VERSION 0.1.${ES_BUILD})
message(STATUS "ExpressionSynth version: ${PROJECT_VERSION}")

add_subdirectory(JUCE)

# Team ID comes from the environment so it isn't committed to the repo.
# Codemagic sets this; locally, export DEVELOPMENT_TEAM before configuring.
if (NOT DEFINED ENV{DEVELOPMENT_TEAM})
    message(WARNING "DEVELOPMENT_TEAM not set - unsigned builds only.")
endif()
set(DEV_TEAM "$ENV{DEVELOPMENT_TEAM}")

# App Group ID must be registered under your Apple Developer account and
# must match between the container app and the AUv3 extension. JUCE
# generates the entitlements for both targets from this - do not also
# hand-write .entitlements files, they will conflict.
set(APP_GROUP_ID "group.me.buchner.expressionsynth")

juce_add_plugin(ExpressionSynth
    COMPANY_NAME "Buchner"
    BUNDLE_ID "me.buchner.expressionsynth"
    PLUGIN_MANUFACTURER_CODE Buch
    PLUGIN_CODE Exsy
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx"
    AU_MAIN_TYPE "kAudioUnitType_MusicEffect"
    PRODUCT_NAME "ExpressionSynth"

    # App Store Connect rejects uploads with no icon, so this is required
    # for TestFlight rather than cosmetic.
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"

    # iOS only supports AUv3. VST3 will not load and must not be listed
    # here for an iOS build.
    FORMATS AUv3 Standalone

    # --- iOS specifics ---
    # This plugin ANALYSES INCOMING AUDIO, so the standalone app needs
    # microphone access. Without this the app launches, runs, and
    # receives nothing but silence - indistinguishable from a broken
    # analyser.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ExpressionSynth analyses live audio input to play the synthesiser."

    # Required for an AUv3 to keep running while the host is foregrounded.
    BACKGROUND_AUDIO_ENABLED TRUE

    # App Groups let the container app and the AUv3 extension share
    # state; JUCE writes the matching entitlements for both targets.
    APP_GROUPS_ENABLED TRUE
    APP_GROUP_IDS "${APP_GROUP_ID}"

    # BOTH keys need all four orientations. An app that does not require
    # full screen must support every orientation for iPad multitasking,
    # and App Store Connect rejects the upload otherwise - at the upload
    # stage, after signing and archiving have already succeeded.
    #
    # The iPhone key matters even for an iPad-targeted app, because it is
    # UISupportedInterfaceOrientations that validation reads.
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    REQUIRES_FULL_SCREEN FALSE
)

target_sources(ExpressionSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/FeatureExtractor.cpp
    Source/ExpressionMapper.cpp
    Source/ArticulationAnalyser.cpp
    Source/Arpeggiator.cpp
    Source/EffectsChain.cpp
    Source/PitchToMidiConverter.cpp
    Source/SynthEngine.cpp
)

target_compile_definitions(ExpressionSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(ExpressionSynth PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if (CMAKE_SYSTEM_NAME STREQUAL "iOS" AND DEV_TEAM)
    foreach(TGT ExpressionSynth_Standalone ExpressionSynth_AUv3)
        if (TARGET ${TGT})
            set_target_properties(${TGT} PROPERTIES
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEV_TEAM}"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2"
            )
        endif()
    endforeach()
endif()
