# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS)
    project(afc_spectrum_helper_unit_tests VERSION ${prplmesh_VERSION})

    # Pure parsing / constant checks only - do not compile afc_spectrum_helper.cpp here.
    # That translation unit needs Ambiorix/WHM; parse_possible_channels_list is header-inline.
    set(unit_tests_sources
        ${CMAKE_CURRENT_LIST_DIR}/afc_spectrum_helper_test.cpp
    )

    add_executable(${PROJECT_NAME}
        ${unit_tests_sources}
    )

    if (COVERAGE)
        set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
        set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "--coverage")
    endif()

    target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/agent/src/beerocks/slave
            ${CMAKE_SOURCE_DIR}/common/beerocks/bcl/include
    )

    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            bcl
            elpp
    )

    install(TARGETS ${PROJECT_NAME} DESTINATION tests)
    add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
endif()
