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

    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/dataelements_network_device_test.odl.in
        ${CMAKE_CURRENT_BINARY_DIR}/dataelements_network_device_test.odl
        @ONLY
    )

    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            IEEE1905_ODL_PATH="${CMAKE_BINARY_DIR}/controller/nbapi/controller/odl/ieee1905.odl"
            DATAELEMENTS_NETWORK_DEVICE_TEST_ODL_PATH="${CMAKE_CURRENT_BINARY_DIR}/dataelements_network_device_test.odl"
    )

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

    add_executable(traffic_separation_event_test
        ${CMAKE_CURRENT_LIST_DIR}/traffic_separation_event_test.cpp
    )
    target_compile_definitions(traffic_separation_event_test
        PRIVATE
            CONTROLLER_ODL_PATH="${CMAKE_BINARY_DIR}/controller/nbapi/controller/odl/controller.odl"
    )
    target_link_libraries(traffic_separation_event_test
        mapfcommon bcl nbapi amxc amxd amxp libamxrt gtest_main)
    target_include_directories(traffic_separation_event_test
        PRIVATE
            ${PLATFORM_INCLUDE_DIR}
    )

    install(TARGETS traffic_separation_event_test DESTINATION tests)
    add_test(NAME traffic_separation_event_test
        COMMAND $<TARGET_FILE:traffic_separation_event_test>)
endif()
