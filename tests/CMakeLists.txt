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
