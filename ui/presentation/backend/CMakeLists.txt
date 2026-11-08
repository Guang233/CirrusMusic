# The backend data sources (docs/ui/presentation.md §2, docs/ui/integration.md): the
# only UI target that links the backend. Gallery never links it.
qt_add_library(cirrus_presentation_backend STATIC
    backend_host.h
    backend_host.cpp
    backend_sources.h
    backend_sources.cpp
    backend_images.h
    backend_images.cpp
    backend_catalog.h
    backend_catalog.cpp
    backend_playback.h
    backend_playback.cpp
    music_requests.h
    music_requests.cpp
    backend_session.h
    backend_session.cpp
    cache_queue.h
    cache_queue.cpp
)
target_compile_features(cirrus_presentation_backend PUBLIC cxx_std_20)
target_include_directories(cirrus_presentation_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_presentation_backend
    PUBLIC cirrus_presentation
    PRIVATE cirrus_backend cirrus_resource_cache qrcodegen Qt6::Core Qt6::Gui)
