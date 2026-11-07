# Nayuki QR Code generator (MIT), unmodified upstream sources; see README.md.
add_library(qrcodegen STATIC qrcodegen.cpp qrcodegen.hpp)
target_include_directories(qrcodegen SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(qrcodegen PUBLIC cxx_std_11)
set_target_properties(qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON)
