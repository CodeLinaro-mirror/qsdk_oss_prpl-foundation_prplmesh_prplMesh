# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

#set default odl folder
message(STATUS  "Using CMake version : "  ${CMAKE_VERSION})

# configure_file_odl func 
include("${CMAKE_SOURCE_DIR}/cmake/ConfigureOdl.cmake")

#search for all available .odl.in files in the odl folder and parse them
set(ODL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/controller)

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/odl/*.odl.in")
list(REMOVE_ITEM files "${CMAKE_CURRENT_SOURCE_DIR}/odl/ieee1905_usp_cfg.odl.in")
foreach(file ${files})
  configure_file_odl(${file} ${ODL_OUTPUT_DIR})
endforeach()

if(NOT "${INCLUDE_IEEE1905_USP_CONFIG}" STREQUAL "")
  configure_file_odl("${CMAKE_CURRENT_SOURCE_DIR}/odl/ieee1905_usp_cfg.odl.in" ${ODL_OUTPUT_DIR})
endif()

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/odl/defaults.d/*.odl.in")
foreach(file ${files})
  configure_file_odl(${file} ${ODL_OUTPUT_DIR})
endforeach()

find_program(AMXO_CG_EXECUTABLE amxo-cg)
# Be permissive if amxo-cg is not found
if(NOT AMXO_CG_EXECUTABLE)
  message(WARNING "${BoldWhite}amxo-cg not found: ignore datamodel parsing.${ColourReset}")
else()
  # Parse master_config.odl file
  message(STATUS "${BoldYellow}Parse odl using amxo${ColourReset} : master_config.odl")
  execute_process(COMMAND ${AMXO_CG_EXECUTABLE} ${ODL_OUTPUT_DIR}/odl/master_config.odl --no-warnings RESULT_VARIABLE DATAMODEL_PARSE_RESULT)

  # If parser find errors - generate FATAL_ERROR
  if(NOT ${DATAMODEL_PARSE_RESULT} EQUAL 0)
    message(FATAL_ERROR "Failed to parse controller data model!")
  else()
    message(STATUS "${BoldGreen}Controller data model parsed successfully!${ColourReset}")
  endif()
endif()

# Install ODL files
install(DIRECTORY ${ODL_OUTPUT_DIR}/odl DESTINATION config/controller)

if(NOT "${INCLUDE_DM_MAPPER_CONFIG}" STREQUAL "")
  install(FILES "${CMAKE_SOURCE_DIR}/controller/nbapi/odl/dm_mapper_cfg" DESTINATION "config/controller/odl" RENAME "dm_mapper_cfg.odl" )
endif()

if(NOT "${INCLUDE_IEEE1905_USP_CONFIG}" STREQUAL "")
  install(FILES "${ODL_OUTPUT_DIR}/odl/ieee1905_usp_cfg.odl" DESTINATION "config/controller/odl")
endif()

if(IS_DIRECTORY ${ODL_OUTPUT_DIR}/odl/defaults.d)
    install(DIRECTORY ${ODL_OUTPUT_DIR}/odl/defaults.d DESTINATION config/controller/odl)
endif()
