cmake_minimum_required(VERSION 3.22)
project(ExpressionSynth VERSION 0.1.0)

add_subdirectory(JUCE)

juce_add_plugin(ExpressionSynth
    COMPANY_NAME "YourName"
    BUNDLE_ID "com.yourname.expressionsynth"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Exsy
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES "Fx"
    AU_MAIN_TYPE "kAudioUnitType_MusicEffect"
    PRODUCT_NAME "ExpressionSynth"
    FORMATS AUv3 Standalone
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ExpressionSynth analyses live audio input."
    BACKGROUND_AUDIO_ENABLED TRUE
    APP_GROUPS_ENABLED TRUE
    APP_GROUP_IDS "group.com.yourname.expressionsynth"
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
)

target_link_libraries(ExpressionSynth PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)
