# Vendored Google material-color-utilities (C++ subset); see README.md for provenance and patches.
add_library(cirrus_mcu STATIC
    cpp/blend/blend.cc
    cpp/cam/cam.cc
    cpp/cam/hct.cc
    cpp/cam/hct_solver.cc
    cpp/cam/viewing_conditions.cc
    cpp/contrast/contrast.cc
    cpp/dislike/dislike.cc
    cpp/dynamiccolor/dynamic_color.cc
    cpp/dynamiccolor/dynamic_scheme.cc
    cpp/dynamiccolor/material_dynamic_colors.cc
    cpp/palettes/tones.cc
    cpp/quantize/celebi.cc
    cpp/quantize/lab.cc
    cpp/quantize/wsmeans.cc
    cpp/quantize/wu.cc
    cpp/scheme/scheme_content.cc
    cpp/scheme/scheme_expressive.cc
    cpp/scheme/scheme_fidelity.cc
    cpp/scheme/scheme_fruit_salad.cc
    cpp/scheme/scheme_monochrome.cc
    cpp/scheme/scheme_neutral.cc
    cpp/scheme/scheme_rainbow.cc
    cpp/scheme/scheme_tonal_spot.cc
    cpp/scheme/scheme_vibrant.cc
    cpp/score/score.cc
    cpp/temperature/temperature_cache.cc
    cpp/utils/utils.cc
)
target_compile_features(cirrus_mcu PUBLIC cxx_std_17)
# Upstream includes are rooted at this directory ("cpp/...").
target_include_directories(cirrus_mcu SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(cirrus_mcu PROPERTIES POSITION_INDEPENDENT_CODE ON)
