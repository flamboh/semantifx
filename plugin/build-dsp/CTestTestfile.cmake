# CMake generated Testfile for 
# Source directory: /tmp/sfx/wt/t6-plugin/plugin
# Build directory: /tmp/sfx/wt/t6-plugin/plugin/build-dsp
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[semantifx_eq_tests]=] "/tmp/sfx/wt/t6-plugin/plugin/build-dsp/semantifx_eq_tests")
set_tests_properties([=[semantifx_eq_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;93;add_test;/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;0;")
add_test([=[semantifx_distortion_tests]=] "/tmp/sfx/wt/t6-plugin/plugin/build-dsp/semantifx_distortion_tests")
set_tests_properties([=[semantifx_distortion_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;97;add_test;/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;0;")
add_test([=[semantifx_capture_tests]=] "/tmp/sfx/wt/t6-plugin/plugin/build-dsp/semantifx_capture_tests")
set_tests_properties([=[semantifx_capture_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;101;add_test;/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;0;")
add_test([=[semantifx_slotpool_tests]=] "/tmp/sfx/wt/t6-plugin/plugin/build-dsp/semantifx_slotpool_tests")
set_tests_properties([=[semantifx_slotpool_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;105;add_test;/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;0;")
add_test([=[semantifx_compressor_tests]=] "/tmp/sfx/wt/t6-plugin/plugin/build-dsp/semantifx_compressor_tests")
set_tests_properties([=[semantifx_compressor_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;109;add_test;/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;0;")
add_test([=[semantifx_reverb_tests]=] "/tmp/sfx/wt/t6-plugin/plugin/build-dsp/semantifx_reverb_tests")
set_tests_properties([=[semantifx_reverb_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;113;add_test;/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;0;")
add_test([=[semantifx_parity_tests]=] "/tmp/sfx/wt/t6-plugin/plugin/build-dsp/semantifx_parity_tests" "/tmp/sfx/wt/t6-plugin/plugin/tests/fixtures/parity" "/tmp/sfx/wt/t6-plugin/plugin/../protocol/reverb/noise_44100.f32")
set_tests_properties([=[semantifx_parity_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;117;add_test;/tmp/sfx/wt/t6-plugin/plugin/CMakeLists.txt;0;")
