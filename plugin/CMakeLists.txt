cmake_minimum_required(VERSION 3.22)

project(Semantifx VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SEMANTIFX_DSP_ONLY "Build only the JUCE-free DSP tests" OFF)

set(SEMANTIFX_DSP_SOURCES
  src/dsp/CompressorEffect.cpp
  src/dsp/DistortionEffect.cpp
  src/dsp/EffectFactory.cpp
  src/dsp/EqEffect.cpp
  src/dsp/ReverbEffect.cpp
)

if(NOT SEMANTIFX_DSP_ONLY)
  include(FetchContent)
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(JUCE)

  set(SEMANTIFX_FORMATS Standalone VST3)
  if(APPLE)
    list(APPEND SEMANTIFX_FORMATS AU)
  endif()

  juce_add_plugin(Semantifx
    COMPANY_NAME "Semantifx"
    BUNDLE_ID "org.semantifx.plugin"
    PLUGIN_MANUFACTURER_CODE Smfx
    PLUGIN_CODE Smeq
    FORMATS ${SEMANTIFX_FORMATS}
    PRODUCT_NAME "Semantifx"
    DESCRIPTION "Prompt-driven audio effects"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Semantifx needs microphone access to capture audio for effect analysis."
    COPY_PLUGIN_AFTER_BUILD TRUE
  )

  target_sources(Semantifx PRIVATE
    src/AnalysisResult.cpp
    src/AnalysisResult.h
    src/generated/EffectParameters.h
    src/CaptureBuffer.h
    src/DistortionMath.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/EqMath.h
    src/EqGraphMath.h
    ${SEMANTIFX_DSP_SOURCES}
  )

  target_compile_definitions(Semantifx PUBLIC
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
  )

  target_compile_definitions(Semantifx PRIVATE
    SEMANTIFX_DEVELOPMENT_REPO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )

  target_link_libraries(Semantifx PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
  )
endif()

enable_testing()
add_executable(semantifx_eq_tests tests/EqMathTests.cpp)
target_include_directories(semantifx_eq_tests PRIVATE src)
add_test(NAME semantifx_eq_tests COMMAND semantifx_eq_tests)

add_executable(semantifx_distortion_tests tests/DistortionMathTests.cpp)
target_include_directories(semantifx_distortion_tests PRIVATE src)
add_test(NAME semantifx_distortion_tests COMMAND semantifx_distortion_tests)

add_executable(semantifx_capture_tests tests/CaptureBufferTests.cpp)
target_include_directories(semantifx_capture_tests PRIVATE src)
add_test(NAME semantifx_capture_tests COMMAND semantifx_capture_tests)

add_executable(semantifx_compressor_tests tests/CompressorMathTests.cpp)
target_include_directories(semantifx_compressor_tests PRIVATE src)
add_test(NAME semantifx_compressor_tests COMMAND semantifx_compressor_tests)

add_executable(semantifx_reverb_tests tests/ReverbMathTests.cpp src/dsp/ReverbEffect.cpp)
target_include_directories(semantifx_reverb_tests PRIVATE src)
add_test(NAME semantifx_reverb_tests COMMAND semantifx_reverb_tests)

add_executable(semantifx_parity_tests tests/ParityTests.cpp ${SEMANTIFX_DSP_SOURCES})
target_include_directories(semantifx_parity_tests PRIVATE src)
add_test(
  NAME semantifx_parity_tests
  COMMAND semantifx_parity_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/parity"
    "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/reverb/noise_44100.f32"
)

if(NOT SEMANTIFX_DSP_ONLY)
  add_executable(semantifx_protocol_tests
    tests/AnalysisResultTests.cpp
    src/AnalysisResult.cpp
  )
  target_include_directories(semantifx_protocol_tests PRIVATE src)
  target_link_libraries(semantifx_protocol_tests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
  )
  add_test(NAME semantifx_protocol_tests COMMAND semantifx_protocol_tests)

  add_executable(semantifx_processor_tests tests/ProcessorStateTests.cpp)
  target_include_directories(semantifx_processor_tests PRIVATE src)
  target_compile_definitions(semantifx_processor_tests PRIVATE JucePlugin_Name="Semantifx")
  target_link_libraries(semantifx_processor_tests PRIVATE
    Semantifx
    juce::juce_audio_utils
    juce::juce_dsp
  )
  add_test(NAME semantifx_processor_tests COMMAND semantifx_processor_tests)

  if(APPLE)
    add_test(
      NAME semantifx_microphone_permission
      COMMAND ${CMAKE_COMMAND}
        "-DPLIST_PATH=$<TARGET_BUNDLE_DIR:Semantifx_Standalone>/Contents/Info.plist"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/MicrophonePermissionTest.cmake
    )
  endif()
endif()
