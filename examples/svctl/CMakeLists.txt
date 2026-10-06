add_executable(svctl main.cpp options.cpp application.cpp)
set_target_properties(svctl PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(svctl PRIVATE sv-client-lib)
install(TARGETS svctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
