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
    # C++ port of the TypeScript 2021/2025 dynamic color specs (see README.md)
    cirrus/dynamic_scheme.cc
    cirrus/dynamic_color.cc
    cirrus/color_spec_2021.cc
    cirrus/color_spec_2025.cc
    cirrus/temperature.cc
)
target_compile_features(cirrus_mcu PUBLIC cxx_std_17)
# M_PI (cpp/dynamiccolor/material_dynamic_colors.cc) is not standard; MSVC defines it
# only on request.
if(MSVC)
    target_compile_definitions(cirrus_mcu PRIVATE _USE_MATH_DEFINES)
endif()
# Upstream includes are rooted at this directory ("cpp/...").
target_include_directories(cirrus_mcu SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(cirrus_mcu PROPERTIES POSITION_INDEPENDENT_CODE ON)
