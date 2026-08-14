# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS)
    # --- AFC Available Spectrum Inquiry JSON parser ---
    project(afc_spectrum_parser_unit_tests VERSION ${prplmesh_VERSION})

    add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/afc_spectrum_parser_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../afc_spectrum_parser.cpp
    )

    if (COVERAGE)
        set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
        set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "--coverage")
    endif()

    target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/..
    )

    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            elpp
            gtest_main
    )

    install(TARGETS ${PROJECT_NAME} DESTINATION tests)
    add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)

    # --- AFC EasyMesh TLV / reason-code / action-opcode coverage ---
    set(afc_tlv_test_name afc_easymesh_tlv_unit_tests)
    add_executable(${afc_tlv_test_name}
        ${CMAKE_CURRENT_LIST_DIR}/afc_easymesh_tlv_test.cpp
    )

    if (COVERAGE)
        set_target_properties(${afc_tlv_test_name} PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
        set_target_properties(${afc_tlv_test_name} PROPERTIES LINK_FLAGS "--coverage")
    endif()

    target_link_libraries(${afc_tlv_test_name}
        PRIVATE
            tlvf
            btlvf
            elpp
            gtest_main
    )

    install(TARGETS ${afc_tlv_test_name} DESTINATION tests)
    add_test(NAME ${afc_tlv_test_name} COMMAND $<TARGET_FILE:${afc_tlv_test_name}>)
endif()

