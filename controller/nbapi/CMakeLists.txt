# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

#set default odl folder
set(DATAMODELS_PATH ${CMAKE_CURRENT_BINARY_DIR}/odl)
message(STATUS  "Using CMake version : "  ${CMAKE_VERSION})

# configure_file_odl func 
include("${CMAKE_SOURCE_DIR}/cmake/ConfigureOdl.cmake")

#search for all available .odl.in files in the odl folder and parse them
file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/odl/*.odl.in")
foreach(file ${files})
  configure_file_odl(${file})
endforeach()

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/odl/defaults.d/*.odl.in")
foreach(file ${files})
  configure_file_odl(${file})
endforeach()

# Parse master_config.odl file
message(STATUS "${BoldYellow}Parse odl using amxo${ColourReset} : master_config.odl")
execute_process(COMMAND amxo-cg ${DATAMODELS_PATH}/master_config.odl --no-warnings RESULT_VARIABLE DATAMODEL_PARSE_RESULT)

# If parser find errors - generate FATAL_ERROR
if(${DATAMODEL_PARSE_RESULT})
    message(FATAL_ERROR "Failed to parse controller data model!")
endif()
message(STATUS "${BoldGreen}Controller data model tested successfully!${ColourReset}")

install(DIRECTORY ${DATAMODELS_PATH} DESTINATION config)
if(IS_DIRECTORY ${DATAMODELS_PATH}/defaults.d)
    install(DIRECTORY ${DATAMODELS_PATH}/defaults.d DESTINATION config/odl)
endif()
