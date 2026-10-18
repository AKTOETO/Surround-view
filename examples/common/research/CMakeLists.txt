add_library(sv-research STATIC connection.cpp scenario.cpp trial_plan.cpp variant_matrix.cpp report_output.cpp quality_metrics.cpp quality_criteria.cpp quality_reference.cpp runner.cpp file_experiment.cpp batch.cpp batch_plan.cpp regular_input.cpp report_comparison.cpp report_evidence.cpp)
target_include_directories(sv-research PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sv-research PUBLIC sv-client-lib PRIVATE sv-core OpenSSL::Crypto)
