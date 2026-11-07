# The backend data sources (docs/ui/presentation.md §2, docs/ui/integration.md): the
# only UI target that links the backend. Gallery never links it.
qt_add_library(cirrus_presentation_backend STATIC
    backend_host.h
    backend_host.cpp
)
target_compile_features(cirrus_presentation_backend PUBLIC cxx_std_20)
target_include_directories(cirrus_presentation_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_presentation_backend
    PUBLIC cirrus_presentation
    PRIVATE cirrus_backend Qt6::Core)
