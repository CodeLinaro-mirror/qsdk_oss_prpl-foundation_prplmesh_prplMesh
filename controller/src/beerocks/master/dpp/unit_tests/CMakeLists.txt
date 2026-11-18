# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS)
    # DPP AKM normalisation (pure string logic, no dependencies).
    add_executable(dpp_akm_test
        ${CMAKE_CURRENT_LIST_DIR}/dpp_akm_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../dpp_akm.cpp
    )
    target_link_libraries(dpp_akm_test gtest_main)
    target_include_directories(dpp_akm_test
        PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/../..
    )
    install(TARGETS dpp_akm_test DESTINATION tests)
    add_test(NAME dpp_akm_test COMMAND $<TARGET_FILE:dpp_akm_test>)

    # DPP Configurator session secret lifecycle (Easy Connect v3.0 6.3.4).
    file(GLOB dpp_protocol_sources ${CMAKE_CURRENT_LIST_DIR}/../*.cpp)
    add_executable(dpp_configurator_session_test
        ${CMAKE_CURRENT_LIST_DIR}/dpp_configurator_session_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../controller_dpp_protocol.cpp
        ${dpp_protocol_sources}
    )
    target_link_libraries(dpp_configurator_session_test
        mapfcommon bcl json-c ${OPENSSL_LIBRARIES} gtest_main)
    target_link_options(dpp_configurator_session_test
        PRIVATE
            -Wl,--unresolved-symbols=ignore-all
    )
    target_include_directories(dpp_configurator_session_test
        PRIVATE
            ${PLATFORM_INCLUDE_DIR}
            ${OPENSSL_INCLUDE_DIR}
            ${CMAKE_CURRENT_LIST_DIR}/../..
    )
    install(TARGETS dpp_configurator_session_test DESTINATION tests)
    add_test(NAME dpp_configurator_session_test
        COMMAND $<TARGET_FILE:dpp_configurator_session_test>)
endif()
