cmake_minimum_required(VERSION 3.22)
project(ExpressionSynth VERSION 0.1.0)

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
set(APP_GROUP_ID "group.com.yourname.expressionsynth")

juce_add_plugin(ExpressionSynth
    COMPANY_NAME "YourName"
    BUNDLE_ID "com.yourname.expressionsynth"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Exsy
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx"
    AU_MAIN_TYPE "kAudioUnitType_MusicEffect"
    PRODUCT_NAME "ExpressionSynth"

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

    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    REQUIRES_FULL_SCREEN FALSE
)

target_sources(ExpressionSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/FeatureExtractor.cpp
    Source/ExpressionMapper.cpp
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

if (CMAKE_SYSTEM_NAME STREQU
