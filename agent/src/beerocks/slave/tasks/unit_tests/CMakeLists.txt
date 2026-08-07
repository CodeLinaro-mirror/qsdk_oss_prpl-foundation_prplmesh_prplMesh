# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS)
    project(higher_layer_collection_task_test VERSION ${prplmesh_VERSION})

    set(unit_tests_sources
        ${CMAKE_CURRENT_LIST_DIR}/higher_layer_collection_task_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../higher_layer_collection_task.cpp
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
        bpl
        btlvf
        gtest_main
        gmock
    )

    target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/agent/src/beerocks/slave
            ${CMAKE_CURRENT_LIST_DIR}/..

            ${CMAKE_SOURCE_DIR}/common/beerocks/bcl/include
            ${CMAKE_SOURCE_DIR}/common/beerocks/bwl/include

            ${CMAKE_SOURCE_DIR}/framework/platform/nbapi/include

            ${CMAKE_BINARY_DIR}/common/beerocks/tlvf/include
    )

    install(TARGETS ${PROJECT_NAME} DESTINATION tests)
    add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)

    set(ifaddrs_impl_test_name higher_layer_collection_task_ifaddrs_impl_test)
    set(ifaddrs_impl_test_sources
        ${CMAKE_CURRENT_LIST_DIR}/higher_layer_collection_task_ifaddrs_impl_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../higher_layer_collection_task_ifaddrs_impl.cpp
    )

    add_executable(${ifaddrs_impl_test_name}
        ${ifaddrs_impl_test_sources}
    )

    if (COVERAGE)
        set_target_properties(${ifaddrs_impl_test_name}
            PROPERTIES COMPILE_FLAGS "--coverage -fPIC -O0")
        set_target_properties(${ifaddrs_impl_test_name}
            PROPERTIES LINK_FLAGS "--coverage")
    endif()

    target_link_libraries(${ifaddrs_impl_test_name}
        PRIVATE
        bcl
        btlvf
        bpl
        gtest_main
        gmock
    )

    target_include_directories(${ifaddrs_impl_test_name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/agent/src/beerocks/slave
            ${CMAKE_CURRENT_LIST_DIR}/..

            ${CMAKE_SOURCE_DIR}/common/beerocks/bcl/include
            ${CMAKE_SOURCE_DIR}/common/beerocks/bwl/include

            ${CMAKE_BINARY_DIR}/common/beerocks/tlvf/include
    )

    install(TARGETS ${ifaddrs_impl_test_name} DESTINATION tests)
    add_test(NAME ${ifaddrs_impl_test_name} COMMAND $<TARGET_FILE:${ifaddrs_impl_test_name}>)
endif()
