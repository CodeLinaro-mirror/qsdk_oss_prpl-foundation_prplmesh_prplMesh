# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2021 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

# Set agent data model path
set(DATAMODELS_PATH ${CMAKE_CURRENT_BINARY_DIR}/odl)
message(STATUS "Using CMake version: ${CMAKE_VERSION}")

# configure_file_odl func 
include("${CMAKE_SOURCE_DIR}/cmake/ConfigureOdl.cmake")

# Process all .odl.in files dynamically
file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/odl/*.odl.in")
foreach(file ${files})
    configure_file_odl(${file})
endforeach()

# Parse agent.odl file
message(STATUS "Parsing ODL using amxo: slave_config.odl")
execute_process(COMMAND amxo-cg ${DATAMODELS_PATH}/slave_config.odl --no-warnings RESULT_VARIABLE DATAMODEL_PARSE_RESULT)

# Check for parser errors
if(${DATAMODEL_PARSE_RESULT})
    message(FATAL_ERROR "Failed to parse agent data model!")
endif()
message(STATUS "Agent data model tested successfully!")

install(DIRECTORY ${DATAMODELS_PATH} DESTINATION config)
if(IS_DIRECTORY ${DATAMODELS_PATH}/defaults.d)
    install(DIRECTORY ${DATAMODELS_PATH}/defaults.d DESTINATION config/odl)
endif()
