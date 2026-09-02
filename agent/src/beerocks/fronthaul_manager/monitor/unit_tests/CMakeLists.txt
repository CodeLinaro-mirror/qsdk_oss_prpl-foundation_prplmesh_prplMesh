# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS)
    project(monitor_db_test VERSION ${prplmesh_VERSION})

    set(unit_tests_sources
        ${CMAKE_CURRENT_LIST_DIR}/monitor_db_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../monitor_db.cpp
    )

    add_executable(${PROJECT_NAME}
        ${unit_tests_sources}
    )

    if (COVERAGE)
        set_target_properties(${PROJECT_NAME}
            PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
        set_target_properties(${PROJECT_NAME}
            PROPERTIES LINK_FLAGS "--coverage")
    endif()

    target_link_libraries(${PROJECT_NAME}
        PRIVATE
        bcl
        btlvf
        bwl
        elpp
        tlvf
        gtest_main
    )

    target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${PLATFORM_INCLUDE_DIR}
    )

    install(TARGETS ${PROJECT_NAME} DESTINATION tests)
    add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
endif()
