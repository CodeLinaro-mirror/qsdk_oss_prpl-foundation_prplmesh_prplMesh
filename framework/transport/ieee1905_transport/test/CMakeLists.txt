if(BUILD_TESTS)
    # Broker Tests
    add_executable(ieee1905_transport_broker_tests
        ieee1905_transport_broker_tests.cpp
    )

    target_link_libraries(ieee1905_transport_broker_tests ieee1905_transport_lib gtest_main)

    install(TARGETS ieee1905_transport_broker_tests DESTINATION tests)
    add_test(NAME ieee1905_transport_broker_tests COMMAND $<TARGET_FILE:ieee1905_transport_broker_tests>)

    add_executable(ieee1905_transport_fragmentation_tests
        ieee1905_transport_fragmentation_tests.cpp
    )

    target_link_libraries(ieee1905_transport_fragmentation_tests
        ieee1905_transport_lib gtest_main gmock)
    target_include_directories(ieee1905_transport_fragmentation_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

    install(TARGETS ieee1905_transport_fragmentation_tests DESTINATION tests)
    add_test(NAME ieee1905_transport_fragmentation_tests
        COMMAND $<TARGET_FILE:ieee1905_transport_fragmentation_tests>)
endif()
