# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2021 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

# configure_file_odl func 
include("${CMAKE_SOURCE_DIR}/cmake/ConfigureOdl.cmake")

#search for all available .odl.in files in the odl folder and parse them
set(ODL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/agent)
set(AGENT_PRIVATE_BRIDGE_IFACE_DEFAULT "${BEEROCKS_BRIDGE_IFACE}")

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/odl/*.odl.in")
foreach(file ${files})
    configure_file_odl(${file} ${ODL_OUTPUT_DIR})
endforeach()

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/odl/defaults.d/*.odl.in")
foreach(file ${files})
  configure_file_odl(${file} ${ODL_OUTPUT_DIR})
endforeach()

# Be permissive if amxo-cg is not found
find_program(AMXO_CG_EXECUTABLE amxo-cg)
if(NOT AMXO_CG_EXECUTABLE)
  message(WARNING "${BoldWhite}amxo-cg not found: ignore datamodel parsing.${ColourReset}")
else()
  # Parse agent.odl file
  message(STATUS "${BoldYellow}Parse odl using amxo${ColourReset} : slave_config.odl")
  execute_process(COMMAND ${AMXO_CG_EXECUTABLE} ${ODL_OUTPUT_DIR}/odl/slave_config.odl --no-warnings RESULT_VARIABLE DATAMODEL_PARSE_RESULT)

  # If parser find errors - generate FATAL_ERROR
  if(NOT ${DATAMODEL_PARSE_RESULT} EQUAL 0)
    message(FATAL_ERROR "Failed to parse agent data model!")
  else()
    message(STATUS "${BoldGreen}Agent data model parsed successfully!${ColourReset}")
  endif()
endif()

# Install ODL files
install(DIRECTORY ${ODL_OUTPUT_DIR}/odl DESTINATION config/agent)

if(IS_DIRECTORY ${ODL_OUTPUT_DIR}/odl/defaults.d)
    install(DIRECTORY ${ODL_OUTPUT_DIR}/odl/defaults.d DESTINATION config/agent/odl)
endif()
