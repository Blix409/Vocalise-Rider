cmake_minimum_required(VERSION 3.22)

project(VocalRider VERSION 1.0.0)

# Where JUCE lives. Default: a folder called JUCE in your home folder (~/JUCE, or C:/Users/<you>/JUCE on Windows).
if(DEFINED ENV{HOME})
    set(VR_DEFAULT_JUCE "$ENV{HOME}/JUCE")
else()
    set(VR_DEFAULT_JUCE "$ENV{USERPROFILE}/JUCE")      # Windows
endif()
set(JUCE_DIR "${VR_DEFAULT_JUCE}" CACHE PATH "Path to the JUCE folder")
add_subdirectory(${JUCE_DIR} JUCE_build)

# Optional: Pro Tools (AAX). Give the folder that contains "Interfaces" from the Avid AAX SDK, e.g.
#   cmake -B build -G Xcode -DAAX_SDK_DIR=/Users/you/aax-sdk-2-9-0 ...
# Without it only Audio Unit and VST3 are built.
set(AAX_SDK_DIR "" CACHE PATH "Path to the Avid AAX SDK (optional, enables the AAX format)")
if(APPLE)
    set(VR_FORMATS AU VST3)            # Audio Unit is Mac only
    set(VR_COPY TRUE)                  # copy into your plugin folders after building
else()
    set(VR_FORMATS VST3)
    set(VR_COPY FALSE)                 # Windows: the installer places the plugin (copying needs admin rights)
endif()
if(AAX_SDK_DIR)
    juce_set_aax_sdk_path(${AAX_SDK_DIR})
    list(APPEND VR_FORMATS AAX)
endif()

juce_add_plugin(VocalRider
    COMPANY_NAME "Daemonj Multimedia"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${VR_COPY}
    PLUGIN_MANUFACTURER_CODE Djmm
    PLUGIN_CODE Vrdr
    FORMATS ${VR_FORMATS}
    AAX_CATEGORY ePlugInCategory_Dynamics
    PRODUCT_NAME "Vocalise-Rider")

target_sources(VocalRider
    PRIVATE
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/RiderCore.h)

# The Vocalise logo and the guide web page (the "i" button), built into the plugin as binary data.
# Rebuild the guide with: python3 Packaging/make_guide.py
juce_add_binary_data(VocalRiderAssets SOURCES Assets/logo.png Assets/guide.html)

# JUCE_DISPLAY_SPLASH_SCREEN=0 hides the "Made with JUCE" splash. This is only permitted with a
# JUCE Indie/Pro license or under GPLv3 (the owner of this project has confirmed a license).
target_compile_definitions(VocalRider
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VocalRider
    PRIVATE
        VocalRiderAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
