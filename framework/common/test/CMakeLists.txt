if(BUILD_TESTS)

	# Encryption Tests
	set(TEST_NAME mapf_common_encryption_tests)
    set(unit_tests_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../encryption.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../hmac_wrapper.cpp
        encryption_test.cpp
    )
    add_executable(${TEST_NAME}
        ${unit_tests_sources}
    )

    if(COVERAGE)
        set_target_properties(${TEST_NAME} PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
        set_target_properties(${TEST_NAME} PROPERTIES LINK_FLAGS "--coverage")
    endif()

    target_include_directories(${TEST_NAME}
        PRIVATE
        ${PLATFORM_INCLUDE_DIR}
        ${OPENSSL_INCLUDE_DIR}
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/../include>
    )
    target_link_libraries(${TEST_NAME} elpp json-c ${OPENSSL_LIBRARIES})

    install(TARGETS ${TEST_NAME} DESTINATION tests)
    add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
endif()
