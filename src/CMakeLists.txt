find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Network Sql Multimedia Gui)

add_library(cirrus_domain INTERFACE)
target_sources(cirrus_domain INTERFACE
    domain/account/session_types.h
    domain/account/qr_login_types.h
    domain/music/music_types.h
    domain/music/catalog_types.h
    domain/music/discovery_types.h
    domain/music/discovery_browse_types.h
    domain/music/cloud_history_types.h
    domain/music/chart_types.h
    domain/music/playlist_discovery_types.h
    domain/music/entity_search_types.h
    domain/music/search_assist_types.h
    domain/music/podcast_types.h
    domain/music/recent_entity_types.h
    domain/music/comment_types.h
    domain/music/comment_write_types.h
    domain/music/together_types.h
    domain/music/account_library_types.h
    domain/music/collection_types.h
    domain/music/playlist_order_types.h
    domain/music/playlist_tag_edit_types.h
    domain/music/playlist_cover_types.h
    domain/music/introduction_types.h
    domain/playback/playback_types.h
    domain/playback/listening_types.h
    domain/playback/listening_state_types.h
    domain/playback/playback_observation_types.h
    domain/playback/listening_history.h
    domain/playback/listening_report.h
)
target_compile_features(cirrus_domain INTERFACE cxx_std_20)
target_include_directories(cirrus_domain INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_domain INTERFACE Qt6::Core)

add_library(cirrus_application STATIC
    application/backend/backend_facade.cpp
    application/backend/backend_facade.h
    application/backend/comment_write_messages.h
    application/backend/comment_write_requests.cpp
    application/backend/comment_write_requests.h
    application/account/login_scheduler.cpp
    application/account/login_scheduler.h
    application/account/qr_login_service.cpp
    application/account/qr_login_service.h
    application/account/session_coordinator.cpp
    application/account/session_coordinator.h
    application/ports/qr_login_gateway.h
    application/ports/music_gateway.h
    application/ports/playback.h
    application/ports/listening_report_gateway.h
    application/ports/collection_gateway.h
    application/ports/comment_write_gateway.h
    application/ports/together_gateway.h
    application/playback/playback_queue.cpp
    application/playback/playback_queue.h
    application/playback/lyric_timeline.cpp
    application/playback/lyric_timeline.h
    application/playback/observed_playback.cpp
    application/playback/observed_playback.h
    application/playback/listening_history.cpp
    application/playback/listening_history.h
    application/playback/listening_fm.cpp
    application/playback/listening_session.cpp
    application/playback/listening_session.h
    application/playback/playback_observation_publisher.cpp
    application/playback/playback_observation_publisher.h
    application/playback/system_media_commands.cpp
    application/playback/system_media_commands.h
    application/playback/playback_coordinator.cpp
    application/playback/playback_coordinator.h
    application/music/personal_fm_source.cpp
    application/music/personal_fm_source.h
    application/music/music_query_service.cpp
    application/music/music_query_service.h
)
target_link_libraries(cirrus_application PUBLIC cirrus_domain)

# Portable wire codec/logical connection. No listener, native API or application reverse dependency.
add_library(cirrus_observation_codec STATIC
    infrastructure/ipc/playback_observation_codec.h
    infrastructure/ipc/playback_observation_codec.cpp
    infrastructure/ipc/playback_observation_request.cpp
    infrastructure/ipc/playback_observation_connection.h
    infrastructure/ipc/playback_observation_connection.cpp
)
target_link_libraries(cirrus_observation_codec PUBLIC cirrus_domain)

# Owner/Core -> mailbox composition, no I/O/native/API scheduler.
add_library(cirrus_observation_feed STATIC
    infrastructure/ipc/playback_observation_feed.h
    infrastructure/ipc/playback_observation_feed.cpp
)
target_link_libraries(cirrus_observation_feed PUBLIC cirrus_application cirrus_observation_codec)

# Portable native command gate/lifecycle. No platform framework by default;
# unsupported start fails closed. Never linked/activated by the product entry.
add_library(cirrus_system_media_native STATIC
    infrastructure/system_media/native_media_inbox.h
    infrastructure/system_media/native_media_inbox.cpp
    infrastructure/system_media/native_media_service.h
    infrastructure/system_media/native_media_service.cpp
    infrastructure/system_media/native_media_platform.h
)
target_link_libraries(cirrus_system_media_native PUBLIC cirrus_application)
option(CIRRUS_BUILD_SMTC "Build the optional Windows SDK/C++ WinRT SMTC adapter" OFF)
option(CIRRUS_BUILD_NOW_PLAYING "Build the optional macOS Now Playing adapter" OFF)
if(CIRRUS_BUILD_SMTC)
    if(NOT WIN32)
        message(FATAL_ERROR "CIRRUS_BUILD_SMTC requires a native Windows SDK and Qt Kit")
    endif()
    target_sources(cirrus_system_media_native PRIVATE infrastructure/system_media/windows_smtc_platform.cpp)
    target_compile_definitions(cirrus_system_media_native PRIVATE CIRRUS_NATIVE_SMTC NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(cirrus_system_media_native PRIVATE windowsapp runtimeobject ole32 user32)
endif()
if(CIRRUS_BUILD_NOW_PLAYING)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(FATAL_ERROR "CIRRUS_BUILD_NOW_PLAYING requires a native macOS SDK and Qt Kit")
    endif()
    enable_language(OBJCXX)
    find_library(CIRRUS_MEDIA_PLAYER_FRAMEWORK MediaPlayer REQUIRED)
    find_library(CIRRUS_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_sources(cirrus_system_media_native PRIVATE infrastructure/system_media/macos_now_playing_platform.mm)
    target_compile_definitions(cirrus_system_media_native PRIVATE CIRRUS_NATIVE_NOW_PLAYING)
    set_target_properties(cirrus_system_media_native PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
    target_compile_options(cirrus_system_media_native PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>" "$<$<COMPILE_LANGUAGE:OBJCXX>:-fblocks>")
    target_link_libraries(cirrus_system_media_native PRIVATE ${CIRRUS_MEDIA_PLAYER_FRAMEWORK} ${CIRRUS_FOUNDATION_FRAMEWORK})
endif()

# Optional native session-bus adapter; never linked/activated by the product entry.
option(CIRRUS_BUILD_MPRIS "Build the optional Linux MPRIS adapter" OFF)
if(CIRRUS_BUILD_MPRIS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "CIRRUS_BUILD_MPRIS requires Linux")
    endif()
    find_package(Qt6 6.10 REQUIRED COMPONENTS DBus)
    add_library(cirrus_system_media_mpris STATIC
        infrastructure/system_media/mpris_service.h
        infrastructure/system_media/mpris_service.cpp
        infrastructure/system_media/mpris_worker.h
        infrastructure/system_media/mpris_worker.cpp
    )
    target_link_libraries(cirrus_system_media_mpris PUBLIC cirrus_observation_feed PRIVATE Qt6::DBus cirrus_observation_linux)
endif()

# Linux resource/credential harness primitive, not composed into the product.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(cirrus_observation_linux STATIC
        infrastructure/ipc/linux_observation_endpoint.h
        infrastructure/ipc/linux_observation_endpoint.cpp
    )
    target_link_libraries(cirrus_observation_linux PUBLIC cirrus_domain)
    add_library(cirrus_observation_linux_service STATIC
        infrastructure/ipc/linux_observation_io.h
        infrastructure/ipc/linux_observation_io.cpp
        infrastructure/ipc/linux_observation_service.h
        infrastructure/ipc/linux_observation_service.cpp
    )
    target_link_libraries(cirrus_observation_linux_service PUBLIC cirrus_observation_linux cirrus_observation_feed)
    add_library(cirrus_observation_linux_client STATIC
        infrastructure/ipc/linux_observation_client.h
        infrastructure/ipc/linux_observation_client.cpp
    )
    target_link_libraries(cirrus_observation_linux_client PUBLIC cirrus_observation_linux cirrus_observation_codec)
endif()

# Native resource/identity boundary, not a product service or media adapter.
# Linux delegates to its existing primitive; Darwin/Windows keep native guards.
add_library(cirrus_observation_local STATIC
    infrastructure/ipc/local_observation_endpoint.h
    infrastructure/ipc/local_observation_endpoint.cpp
)
target_link_libraries(cirrus_observation_local PUBLIC cirrus_domain)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(cirrus_observation_local PRIVATE cirrus_observation_linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_sources(cirrus_observation_local PRIVATE infrastructure/ipc/macos_observation_endpoint.cpp)
elseif(WIN32)
    target_sources(cirrus_observation_local PRIVATE infrastructure/ipc/windows_observation_endpoint.cpp)
    target_compile_definitions(cirrus_observation_local PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(cirrus_observation_local PRIVATE advapi32)
endif()

add_library(cirrus_netease_protocol STATIC
    infrastructure/netease/protocol/eapi_request_profile.cpp
    infrastructure/netease/protocol/eapi_request_profile.h
    infrastructure/netease/protocol/form_encoding.cpp
    infrastructure/netease/protocol/form_encoding.h
    infrastructure/netease/protocol/official_route.cpp
    infrastructure/netease/protocol/official_route.h
    infrastructure/netease/protocol/openssl_primitives.cpp
    infrastructure/netease/protocol/openssl_primitives.h
    infrastructure/netease/protocol/protocol_crypto.cpp
    infrastructure/netease/protocol/protocol_crypto.h
    infrastructure/netease/protocol/response_decoder.cpp
    infrastructure/netease/protocol/response_decoder.h
    infrastructure/netease/protocol/xeapi_crypto.cpp
    infrastructure/netease/protocol/xeapi_crypto.h
    infrastructure/netease/protocol/xeapi_request_profile.cpp
    infrastructure/netease/protocol/xeapi_request_profile.h
)

target_compile_features(cirrus_netease_protocol PUBLIC cxx_std_20)
target_include_directories(cirrus_netease_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_netease_protocol PUBLIC Qt6::Core PRIVATE OpenSSL::Crypto ZLIB::ZLIB)

add_library(cirrus_netease_network STATIC
    infrastructure/netease/network/http_transport.cpp
    infrastructure/netease/network/http_transport.h
)
target_compile_features(cirrus_netease_network PUBLIC cxx_std_20)
target_include_directories(cirrus_netease_network PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_netease_network PUBLIC cirrus_netease_protocol Qt6::Network)

add_library(cirrus_netease_session STATIC
    infrastructure/netease/session/session_cookie_store.cpp
    infrastructure/netease/session/session_cookie_store.h
)
target_link_libraries(cirrus_netease_session PUBLIC cirrus_domain Qt6::Network)

add_library(cirrus_netease_api STATIC
    infrastructure/netease/api/api_operation.cpp
    infrastructure/netease/api/api_operation.h
    infrastructure/netease/api/discovery_browse_operation.cpp
    infrastructure/netease/api/discovery_browse_operation.h
    infrastructure/netease/api/together_operation.cpp
    infrastructure/netease/api/together_operation.h
    infrastructure/netease/api/comment_operation.cpp
    infrastructure/netease/api/comment_operation.h
    infrastructure/netease/api/comment_write_operation.cpp
    infrastructure/netease/api/comment_write_operation.h
    infrastructure/netease/api/collection_operation.cpp
    infrastructure/netease/api/collection_operation.h
    infrastructure/netease/api/session_api_client.cpp
    infrastructure/netease/api/session_api_client.h
    infrastructure/netease/api/xeapi_key_registrar.cpp
    infrastructure/netease/api/xeapi_key_registrar.h
    infrastructure/netease/api/xeapi_key_ring.cpp
    infrastructure/netease/api/xeapi_key_ring.h
)
target_link_libraries(cirrus_netease_api PUBLIC cirrus_netease_network cirrus_netease_session)

# SQLite via Qt SQL (bundled QSQLITE driver). Not linked by domain/application.
add_library(cirrus_storage STATIC
    infrastructure/storage/database_worker.cpp
    infrastructure/storage/database_worker.h
    infrastructure/storage/installation_store.cpp
    infrastructure/storage/installation_store.h
)
target_compile_features(cirrus_storage PUBLIC cxx_std_20)
target_include_directories(cirrus_storage PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_storage PUBLIC Qt6::Sql)

# Optional Runtime composition uses the existing database executor; Core has no SQL dependency.
add_library(cirrus_listening_state STATIC
    infrastructure/storage/listening_state_store.h
    infrastructure/storage/listening_state_store.cpp
    infrastructure/storage/listening_state_service.h
    infrastructure/storage/listening_state_service.cpp
)
target_link_libraries(cirrus_listening_state PUBLIC cirrus_application cirrus_storage)

# System keyring (QtKeychain system package). Plaintext fallback is disabled.
find_package(Qt6Keychain REQUIRED)
add_library(cirrus_secrets STATIC
    infrastructure/secrets/tracked_secret_store.cpp
    infrastructure/secrets/tracked_secret_store.h
    infrastructure/secrets/secret_store.cpp
    infrastructure/secrets/secret_store.h
)
target_compile_features(cirrus_secrets PUBLIC cxx_std_20)
target_include_directories(cirrus_secrets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_secrets PUBLIC Qt6::Core PRIVATE Qt6Keychain::Qt6Keychain)

# Composition-level glue between protocol state, storage and secrets.
add_library(cirrus_bootstrap STATIC
    infrastructure/bootstrap/anonymous_bootstrap.cpp
    infrastructure/bootstrap/anonymous_bootstrap.h
    infrastructure/bootstrap/xeapi_key_vault.cpp
    infrastructure/bootstrap/xeapi_key_vault.h
)
target_link_libraries(cirrus_bootstrap PUBLIC cirrus_netease_api cirrus_secrets cirrus_storage)

add_library(cirrus_netease_auth STATIC
    infrastructure/netease/auth/account_credential_vault.cpp
    infrastructure/netease/auth/account_credential_vault.h
    infrastructure/netease/auth/account_identity_mapper.cpp
    infrastructure/netease/auth/account_identity_mapper.h
    infrastructure/netease/auth/qr_login_gateway.cpp
    infrastructure/netease/auth/qr_login_gateway.h
)
target_link_libraries(cirrus_netease_auth PUBLIC cirrus_application cirrus_netease_api cirrus_secrets)

add_library(cirrus_netease_music STATIC
    infrastructure/netease/music/together_gateway.cpp
    infrastructure/netease/music/together_gateway.h
    infrastructure/netease/music/comment_write_gateway.cpp
    infrastructure/netease/music/comment_write_gateway.h
    infrastructure/netease/music/comment_v3_issuer.cpp
    infrastructure/netease/music/comment_v3_issuer.h
    infrastructure/netease/music/cover_upload.cpp
    infrastructure/netease/music/cover_upload.h
    infrastructure/netease/music/collection_gateway.cpp
    infrastructure/netease/music/collection_gateway.h
    infrastructure/netease/music/recommendation_history_mapper.cpp
    infrastructure/netease/music/recommendation_history_mapper.h
    infrastructure/netease/music/account_library_mapper.cpp
    infrastructure/netease/music/account_library_mapper.h
    infrastructure/netease/music/comment_mapper.cpp
    infrastructure/netease/music/comment_mapper.h
    infrastructure/netease/music/listening_report_gateway.cpp
    infrastructure/netease/music/listening_report_gateway.h
    infrastructure/netease/music/stream_resolver.cpp
    infrastructure/netease/music/stream_resolver.h
    infrastructure/netease/music/lyric_parser.cpp
    infrastructure/netease/music/lyric_parser.h
    infrastructure/netease/music/podcast_mapper.cpp
    infrastructure/netease/music/podcast_mapper.h
    infrastructure/netease/music/recent_entity_mapper.cpp
    infrastructure/netease/music/recent_entity_mapper.h
    infrastructure/netease/music/spoken_mapping.cpp
    infrastructure/netease/music/spoken_mapping.h
    infrastructure/netease/music/search_assist_mapper.cpp
    infrastructure/netease/music/search_assist_mapper.h
    infrastructure/netease/music/entity_search_mapper.cpp
    infrastructure/netease/music/entity_search_mapper.h
    infrastructure/netease/music/playlist_discovery_mapper.cpp
    infrastructure/netease/music/playlist_discovery_mapper.h
    infrastructure/netease/music/chart_mapper.cpp
    infrastructure/netease/music/chart_mapper.h
    infrastructure/netease/music/cloud_history_mapper.cpp
    infrastructure/netease/music/cloud_history_mapper.h
    infrastructure/netease/music/library_mapper.cpp
    infrastructure/netease/music/library_mapper.h
    infrastructure/netease/music/discovery_mapper.cpp
    infrastructure/netease/music/discovery_mapper.h
    infrastructure/netease/music/discovery_browse_mapper.cpp
    infrastructure/netease/music/discovery_browse_mapper.h
    infrastructure/netease/music/song_wiki_text_mapper.cpp
    infrastructure/netease/music/song_wiki_text_mapper.h
    infrastructure/netease/music/catalog_mapper.cpp
    infrastructure/netease/music/catalog_mapper.h
    infrastructure/netease/music/introduction_mapper.cpp
    infrastructure/netease/music/introduction_mapper.h
    infrastructure/netease/music/metadata_mapping.h
    infrastructure/netease/music/music_mapping.h
    infrastructure/netease/music/music_mapper.cpp
    infrastructure/netease/music/music_mapper.h
    infrastructure/netease/music/music_gateway.cpp
    infrastructure/netease/music/music_gateway.h
)
target_link_libraries(cirrus_netease_music PUBLIC cirrus_application cirrus_netease_api PRIVATE Qt6::Gui)

add_library(cirrus_media STATIC
    infrastructure/media/media_source_policy.h
    infrastructure/media/media_buffer.h
    infrastructure/media/media_buffer.cpp
    infrastructure/media/qt_playback_backend.h
    infrastructure/media/qt_playback_backend.cpp
)
target_link_libraries(cirrus_media PUBLIC cirrus_application Qt6::Network Qt6::Multimedia)

# Composition stays out of application/domain and is not linked into the template UI.
add_library(cirrus_backend STATIC
    infrastructure/runtime/backend_runtime.cpp
    infrastructure/runtime/backend_runtime.h
)
target_link_libraries(cirrus_backend PUBLIC cirrus_application
    PRIVATE cirrus_bootstrap cirrus_netease_auth cirrus_netease_music cirrus_media cirrus_listening_state)
