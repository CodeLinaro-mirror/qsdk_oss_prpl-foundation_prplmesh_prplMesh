# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

# AgentDB is also used by multi_vendor in controller-only builds. Define its
# library independently of the Agent executable and keep one owner per process.
add_library(agentdb
    ${CMAKE_CURRENT_SOURCE_DIR}/../agent_db.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../agent_datamodel.cpp
)

set_target_properties(agentdb PROPERTIES
    VERSION ${prplmesh_VERSION}
    SOVERSION ${prplmesh_VERSION_MAJOR}
    LINK_FLAGS "-Wl,-z,defs"
)

target_include_directories(agentdb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<TARGET_PROPERTY:bwl,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(agentdb PUBLIC bcl bpl btlvf nbapi)

if (ENABLE_NBAPI)
    target_compile_definitions(agentdb PRIVATE ENABLE_NBAPI)
endif()

install(TARGETS agentdb
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
