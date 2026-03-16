# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS AND ENABLE_NBAPI)
    project(ieee1905_task_test VERSION ${prplmesh_VERSION})

    set(unit_tests_sources
        ${CMAKE_CURRENT_LIST_DIR}/ieee1905_task_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../db/db.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../task.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../ieee1905_task.cpp
        ${CMAKE_SOURCE_DIR}/controller/nbapi/on_action.cpp
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

    target_compile_definitions(${PROJECT_NAME}
        PRIVATE IEEE1905_ODL_PATH="${CMAKE_BINARY_DIR}/controller/nbapi/controller/odl/ieee1905.odl")

    target_link_libraries(${PROJECT_NAME}
        mapfcommon bpl bcl btl btlvf nbapi amxc amxd amxp libamxrt gtest_main gmock)
    target_link_options(${PROJECT_NAME}
        PRIVATE
            -Wl,--unresolved-symbols=ignore-all
    )
    target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${PLATFORM_INCLUDE_DIR}
            ${CMAKE_SOURCE_DIR}/common/beerocks/btl/include
            ${CMAKE_SOURCE_DIR}/controller/nbapi
            ${CMAKE_SOURCE_DIR}/controller/src/beerocks/master/db
    )

    install(TARGETS ${PROJECT_NAME} DESTINATION tests)
    add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
endif()
