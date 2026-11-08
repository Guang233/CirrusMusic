# Audio engine PoC (docs/tasks/audio-engine.md §6 stage A); off by default, not shipped.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CIRRUS_POC_FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswresample)
find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)

qt_add_executable(cirrus_audio_poc
    main.cpp
    decoder.h decoder.cpp
    chain.h chain.cpp
    player.h player.cpp
)
target_link_libraries(cirrus_audio_poc PRIVATE Qt6::Core Qt6::Multimedia PkgConfig::CIRRUS_POC_FFMPEG)
