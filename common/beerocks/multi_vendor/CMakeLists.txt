# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2022 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

project(multi_vendor VERSION ${prplmesh_VERSION})

message("${BoldWhite}Preparing ${BoldGreen}${PROJECT_NAME}${BoldWhite} for the ${BoldGreen}${TARGET_PLATFORM}${BoldWhite} platform${ColourReset}")

# Set the base path for the current module
set(MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

# New Vendor has to set the path of their cpp file like example.
# Note: Add the path of utils files first.
list(APPEND multi_vendor_sources ${MODULE_PATH}/multi_vendor.cpp)
list(APPEND multi_vendor_sources ${MODULE_PATH}/example/src/tlvf_vendor_example_utils.cpp)
list(APPEND multi_vendor_sources ${MODULE_PATH}/example/src/multi_vendor_example.cpp)
list(APPEND multi_vendor_sources ${MODULE_PATH}/airties/src/tlvf_airties_utils.cpp)
list(APPEND multi_vendor_sources ${MODULE_PATH}/airties/src/multi_vendor_airties.cpp)

find_package(amxb REQUIRED)
find_package(amxc REQUIRED)
find_package(amxd REQUIRED)
find_package(amxp REQUIRED)
list(APPEND MULTI_VENDOR_LIBS amxd amxp amxb amxc)

# Build the library
add_library(${PROJECT_NAME} ${multi_vendor_sources})

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${prplmesh_VERSION} SOVERSION ${prplmesh_VERSION_MAJOR})
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-z,defs")

target_link_libraries(${PROJECT_NAME} PRIVATE ${MULTI_VENDOR_LIBS} bcl bwl btlvf tlvf nbapi bpl mapfcommon)

# Install
# New Vendor has to add the path of header files like example.
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/example/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/airties/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../agent/src/beerocks/slave/>
    )

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
