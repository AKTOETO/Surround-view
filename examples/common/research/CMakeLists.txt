add_library(sv-research STATIC connection.cpp scenario.cpp runner.cpp)
target_include_directories(sv-research PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sv-research PUBLIC sv-client-lib PRIVATE OpenSSL::Crypto)
