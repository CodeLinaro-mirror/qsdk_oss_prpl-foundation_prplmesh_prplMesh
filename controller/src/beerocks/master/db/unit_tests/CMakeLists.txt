# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS)
    project(db_unit_tests VERSION ${prplmesh_VERSION})

    set(db_unit_tests_sources
        ${db_unit_tests}
        ${CMAKE_CURRENT_LIST_DIR}/db_test.cpp
    )

    set(dpp_unit_tests_sources
        ${CMAKE_CURRENT_LIST_DIR}/test_dpp_auth_request.cpp
        ${CMAKE_CURRENT_LIST_DIR}/test_dpp_auth_response.cpp
        ${CMAKE_CURRENT_LIST_DIR}/test_dpp_configuration.cpp
        ${CMAKE_CURRENT_LIST_DIR}/test_dpp_database.cpp
        ${CMAKE_CURRENT_LIST_DIR}/test_dpp_primitives.cpp
        ${CMAKE_CURRENT_LIST_DIR}/test_dpp_transport_tlvs.cpp
        # Compile the DPP implementation without the controller application entry point.
        ${CMAKE_CURRENT_LIST_DIR}/../../controller_dpp_protocol.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../controller_dpp_configuration.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../controller_dpp_csign.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../dpp/dpp_attributes.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../dpp/dpp_config.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../dpp/dpp_connector.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../dpp/dpp_crypto.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../dpp/dpp_gas.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../dpp/dpp_jws.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../dpp/dpp_utils.cpp
    )

    # Both suites exercise the database, but DPP tests belong only to their own target.
    foreach(test_target db_unit_tests dpp_unit_tests)
        add_executable(${test_target}
            ${${test_target}_sources}
            ${CMAKE_CURRENT_LIST_DIR}/../db.cpp
            ${CMAKE_CURRENT_LIST_DIR}/../station.cpp
        )
        if (COVERAGE)
            set_target_properties(${test_target} PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
            set_target_properties(${test_target} PROPERTIES LINK_FLAGS "--coverage")
        endif()
        target_link_libraries(${test_target} mapfcommon bpl btlvf nbapi gtest_main gmock bcl json-c)
        target_include_directories(${test_target}
            PRIVATE
                ${PLATFORM_INCLUDE_DIR}
                ${CMAKE_CURRENT_LIST_DIR}/../..
                ${CMAKE_CURRENT_LIST_DIR}/../../../..
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        )

        install(TARGETS ${test_target} DESTINATION tests)
        add_test(NAME ${test_target} COMMAND $<TARGET_FILE:${test_target}>)
    endforeach()
endif()

