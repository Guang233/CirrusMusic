find_package(Qt6 REQUIRED COMPONENTS Test Sql)

qt_add_executable(tst_netease_protocol
    unit/netease_protocol_test.cpp
)
target_link_libraries(tst_netease_protocol PRIVATE cirrus_netease_protocol Qt6::Test)
add_test(NAME netease_protocol COMMAND tst_netease_protocol)
set_tests_properties(netease_protocol PROPERTIES TIMEOUT 15)

qt_add_executable(tst_netease_crypto unit/netease_crypto_test.cpp)
target_link_libraries(tst_netease_crypto PRIVATE cirrus_netease_protocol Qt6::Test)
add_test(NAME netease_crypto COMMAND tst_netease_crypto)
set_tests_properties(netease_crypto PROPERTIES TIMEOUT 15)

qt_add_executable(tst_http_transport unit/http_transport_test.cpp)
target_link_libraries(tst_http_transport PRIVATE cirrus_netease_network Qt6::Test)
add_test(NAME http_transport COMMAND tst_http_transport)
set_tests_properties(http_transport PROPERTIES TIMEOUT 15)

qt_add_executable(tst_response_decoder unit/response_decoder_test.cpp)
target_link_libraries(tst_response_decoder PRIVATE cirrus_netease_protocol Qt6::Test)
add_test(NAME response_decoder COMMAND tst_response_decoder)
set_tests_properties(response_decoder PROPERTIES TIMEOUT 15)

# Application test deliberately links no Network, OpenSSL or infrastructure target.
qt_add_executable(tst_session_coordinator unit/session_coordinator_test.cpp)
target_link_libraries(tst_session_coordinator PRIVATE cirrus_application Qt6::Test)
add_test(NAME session_coordinator COMMAND tst_session_coordinator)
set_tests_properties(session_coordinator PROPERTIES TIMEOUT 15)

qt_add_executable(tst_session_cookie_store unit/session_cookie_store_test.cpp)
target_link_libraries(tst_session_cookie_store PRIVATE cirrus_application cirrus_netease_session Qt6::Test)
add_test(NAME session_cookie_store COMMAND tst_session_cookie_store)
set_tests_properties(session_cookie_store PROPERTIES TIMEOUT 15)

qt_add_executable(tst_session_api_client unit/session_api_client_test.cpp)
target_link_libraries(tst_session_api_client PRIVATE cirrus_netease_api Qt6::Test)
add_test(NAME session_api_client COMMAND tst_session_api_client)
set_tests_properties(session_api_client PROPERTIES TIMEOUT 15)

qt_add_executable(tst_xeapi unit/xeapi_test.cpp)
target_link_libraries(tst_xeapi PRIVATE cirrus_netease_api Qt6::Test)
add_test(NAME xeapi COMMAND tst_xeapi)
set_tests_properties(xeapi PROPERTIES TIMEOUT 15)

qt_add_executable(tst_storage unit/storage_test.cpp)
target_link_libraries(tst_storage PRIVATE cirrus_storage Qt6::Test)
add_test(NAME storage COMMAND tst_storage)
set_tests_properties(storage PROPERTIES TIMEOUT 15)

qt_add_executable(tst_secrets unit/secrets_test.cpp)
target_link_libraries(tst_secrets PRIVATE cirrus_bootstrap Qt6::Test)
add_test(NAME secrets COMMAND tst_secrets)
set_tests_properties(secrets PROPERTIES TIMEOUT 30)

qt_add_executable(tst_bootstrap unit/bootstrap_test.cpp)
target_link_libraries(tst_bootstrap PRIVATE cirrus_bootstrap Qt6::Test)
add_test(NAME bootstrap COMMAND tst_bootstrap)
set_tests_properties(bootstrap PROPERTIES TIMEOUT 15)

qt_add_executable(tst_qr_login_gateway unit/qr_login_gateway_test.cpp)
target_link_libraries(tst_qr_login_gateway PRIVATE cirrus_netease_auth Qt6::Test)
add_test(NAME qr_login_gateway COMMAND tst_qr_login_gateway)
set_tests_properties(qr_login_gateway PROPERTIES TIMEOUT 15)

qt_add_executable(tst_qr_login_service unit/qr_login_service_test.cpp)
target_link_libraries(tst_qr_login_service PRIVATE cirrus_application Qt6::Test)
add_test(NAME qr_login_service COMMAND tst_qr_login_service)
set_tests_properties(qr_login_service PROPERTIES TIMEOUT 15)

qt_add_executable(tst_auth_transaction unit/auth_transaction_test.cpp)
target_link_libraries(tst_auth_transaction PRIVATE cirrus_netease_auth Qt6::Test)
add_test(NAME auth_transaction COMMAND tst_auth_transaction)
set_tests_properties(auth_transaction PROPERTIES TIMEOUT 15)

qt_add_executable(tst_account_restore unit/account_restore_test.cpp)
target_link_libraries(tst_account_restore PRIVATE cirrus_netease_auth Qt6::Test)
add_test(NAME account_restore COMMAND tst_account_restore)
set_tests_properties(account_restore PROPERTIES TIMEOUT 20)

qt_add_executable(tst_backend_facade unit/backend_facade_test.cpp)
target_link_libraries(tst_backend_facade PRIVATE cirrus_application Qt6::Test)
add_test(NAME backend_facade COMMAND tst_backend_facade)
set_tests_properties(backend_facade PROPERTIES TIMEOUT 15)

qt_add_executable(tst_backend_runtime unit/backend_runtime_test.cpp)
target_link_libraries(tst_backend_runtime PRIVATE cirrus_backend cirrus_bootstrap Qt6::Test)
add_test(NAME backend_runtime COMMAND tst_backend_runtime)
set_tests_properties(backend_runtime PROPERTIES TIMEOUT 30)

qt_add_executable(tst_music unit/music_test.cpp)
target_link_libraries(tst_music PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME music COMMAND tst_music)
set_tests_properties(music PROPERTIES TIMEOUT 15)

qt_add_executable(tst_stream unit/stream_test.cpp)
target_link_libraries(tst_stream PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME stream COMMAND tst_stream)
set_tests_properties(stream PROPERTIES TIMEOUT 15)

# Keep the coordinator independently linkable using Core/Test only.
qt_add_executable(tst_playback unit/playback_test.cpp)
target_link_libraries(tst_playback PRIVATE cirrus_application Qt6::Test)
add_test(NAME playback COMMAND tst_playback)
set_tests_properties(playback PROPERTIES TIMEOUT 15)

qt_add_executable(tst_media unit/media_test.cpp)
target_link_libraries(tst_media PRIVATE cirrus_media Qt6::Test)
add_test(NAME media COMMAND tst_media)
set_tests_properties(media PROPERTIES TIMEOUT 30)

qt_add_executable(tst_listening unit/listening_test.cpp)
target_link_libraries(tst_listening PRIVATE cirrus_application Qt6::Test)
add_test(NAME listening COMMAND tst_listening)
set_tests_properties(listening PROPERTIES TIMEOUT 15)

qt_add_executable(tst_catalog unit/catalog_test.cpp)
target_link_libraries(tst_catalog PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME catalog COMMAND tst_catalog)
set_tests_properties(catalog PROPERTIES TIMEOUT 15)

qt_add_executable(tst_library unit/library_test.cpp)
target_link_libraries(tst_library PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME library COMMAND tst_library)
set_tests_properties(library PROPERTIES TIMEOUT 20)

qt_add_executable(tst_discovery unit/discovery_test.cpp)
target_link_libraries(tst_discovery PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME discovery COMMAND tst_discovery)
set_tests_properties(discovery PROPERTIES TIMEOUT 15)

# Dynamic FM supply stays Core-only, independently of provider/network/media.
qt_add_executable(tst_personal_fm unit/personal_fm_test.cpp)
target_link_libraries(tst_personal_fm PRIVATE cirrus_application Qt6::Test)
add_test(NAME personal_fm COMMAND tst_personal_fm)
set_tests_properties(personal_fm PROPERTIES TIMEOUT 15)

qt_add_executable(tst_fm_listening unit/fm_listening_test.cpp)
target_link_libraries(tst_fm_listening PRIVATE cirrus_application Qt6::Test)
add_test(NAME fm_listening COMMAND tst_fm_listening)
set_tests_properties(fm_listening PROPERTIES TIMEOUT 20)

qt_add_executable(tst_listening_history unit/listening_history_test.cpp)
target_link_libraries(tst_listening_history PRIVATE cirrus_application Qt6::Test)
add_test(NAME listening_history COMMAND tst_listening_history)
set_tests_properties(listening_history PROPERTIES TIMEOUT 20)

qt_add_executable(tst_listening_report unit/listening_report_test.cpp)
target_link_libraries(tst_listening_report PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME listening_report COMMAND tst_listening_report)
set_tests_properties(listening_report PROPERTIES TIMEOUT 20)

qt_add_executable(tst_charts unit/charts_test.cpp)
target_link_libraries(tst_charts PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME charts COMMAND tst_charts)
set_tests_properties(charts PROPERTIES TIMEOUT 20)

qt_add_executable(tst_cloud_history unit/cloud_history_test.cpp)
target_link_libraries(tst_cloud_history PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME cloud_history COMMAND tst_cloud_history)
set_tests_properties(cloud_history PROPERTIES TIMEOUT 20)
