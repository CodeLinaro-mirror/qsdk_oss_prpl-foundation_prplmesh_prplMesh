# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

if (BUILD_TESTS AND ENABLE_NBAPI)
    project(mesh_pbc_template_actions_test VERSION ${prplmesh_VERSION})

    find_package(amxc REQUIRED)
    find_package(amxd REQUIRED)
    find_package(amxo REQUIRED)
    find_package(amxp REQUIRED)
    find_package(libamxrt REQUIRED)

    set(unit_tests_sources
        ${CMAKE_CURRENT_LIST_DIR}/mesh_pbc_session_actions_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mesh_pbc_session_task_stub.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mesh_pbc_template_actions_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/mesh_pbc_test_fixture.cpp
        ${CMAKE_CURRENT_LIST_DIR}/pbc_onboarding_session_test.cpp
        ${CMAKE_CURRENT_LIST_DIR}/son_actions_stub.cpp
        ${CMAKE_CURRENT_LIST_DIR}/templates_actions_stub.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../mesh_pbc_session_actions.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../mesh_pbc_template_actions.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../on_action.cpp
        ${CMAKE_SOURCE_DIR}/controller/src/beerocks/master/db/db.cpp
        ${CMAKE_SOURCE_DIR}/controller/src/beerocks/master/db/station.cpp
        ${CMAKE_SOURCE_DIR}/controller/src/beerocks/master/tasks/pbc_onboarding_task.cpp
        ${CMAKE_SOURCE_DIR}/controller/src/beerocks/master/tasks/task.cpp
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
        PRIVATE
            ENABLE_NBAPI
            CONTROLLER_ODL_PATH="${CMAKE_BINARY_DIR}/controller/nbapi/controller/odl/controller.odl"
    )

    target_link_libraries(${PROJECT_NAME}
        mapfcommon bpl bcl btl btlvf nbapi amxc amxd amxo amxp libamxrt gtest_main)
    # Workaround: on_action.cpp, db.cpp and tasks/pbc_onboarding_task.cpp call son::Controller,
    # son_actions and templates_actions functions. The ones that no test runs stay unresolved;
    # the ones that a test runs have a stub in this directory.
    target_link_options(${PROJECT_NAME}
        PRIVATE
            -Wl,--unresolved-symbols=ignore-all
    )
    target_include_directories(${PROJECT_NAME}
        PRIVATE
            ${PLATFORM_INCLUDE_DIR}
            ${CMAKE_SOURCE_DIR}/common/beerocks/btl/include
            ${CMAKE_SOURCE_DIR}/controller/nbapi
            ${CMAKE_SOURCE_DIR}/controller/src/beerocks/master
            ${CMAKE_SOURCE_DIR}/controller/src/beerocks/master/db
    )

    install(TARGETS ${PROJECT_NAME} DESTINATION tests)
    add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
endif()
