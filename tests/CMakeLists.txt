find_package(Qt6 REQUIRED COMPONENTS Test)

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

qt_add_executable(tst_qr_login_gateway unit/qr_login_gateway_test.cpp)
target_link_libraries(tst_qr_login_gateway PRIVATE cirrus_netease_auth Qt6::Test)
add_test(NAME qr_login_gateway COMMAND tst_qr_login_gateway)
set_tests_properties(qr_login_gateway PROPERTIES TIMEOUT 15)
