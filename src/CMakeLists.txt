find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
find_package(Qt6 REQUIRED COMPONENTS Network)

add_library(cirrus_netease_protocol STATIC
    infrastructure/netease/protocol/form_encoding.cpp
    infrastructure/netease/protocol/form_encoding.h
    infrastructure/netease/protocol/official_route.cpp
    infrastructure/netease/protocol/official_route.h
    infrastructure/netease/protocol/protocol_crypto.cpp
    infrastructure/netease/protocol/protocol_crypto.h
)

target_compile_features(cirrus_netease_protocol PUBLIC cxx_std_20)
target_include_directories(cirrus_netease_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_netease_protocol PUBLIC Qt6::Core PRIVATE OpenSSL::Crypto)

add_library(cirrus_netease_network STATIC
    infrastructure/netease/network/http_transport.cpp
    infrastructure/netease/network/http_transport.h
)
target_compile_features(cirrus_netease_network PUBLIC cxx_std_20)
target_include_directories(cirrus_netease_network PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_netease_network PUBLIC cirrus_netease_protocol Qt6::Network)
