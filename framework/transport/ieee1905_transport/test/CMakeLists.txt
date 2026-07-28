if(BUILD_TESTS)
    # Broker Tests
    set(unit_tests_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/../ieee1905_transport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ieee1905_transport_broker.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ieee1905_transport_network.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ieee1905_transport_local_bus.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ieee1905_transport_packet_processing.cpp
        ieee1905_transport_broker_tests.cpp
    )
    add_executable(ieee1905_transport_broker_tests
        ${unit_tests_sources}
    )

    if(COVERAGE)
        set_target_properties(ieee1905_transport_broker_tests PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
        set_target_properties(ieee1905_transport_broker_tests PROPERTIES LINK_FLAGS "--coverage")
    endif()

    target_include_directories(ieee1905_transport_broker_tests
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    )
    target_link_libraries(ieee1905_transport_broker_tests
        bpl bcl tlvf btlvf ieee1905_transport_messages mapfcommon elpp gtest_main)

    install(TARGETS ieee1905_transport_broker_tests DESTINATION tests)
    add_test(NAME ieee1905_transport_broker_tests COMMAND $<TARGET_FILE:ieee1905_transport_broker_tests>)
endif()
