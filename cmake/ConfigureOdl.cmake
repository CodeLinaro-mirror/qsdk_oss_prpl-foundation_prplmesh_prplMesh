# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
#
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.

# The `configure_file_odl` function preprocesses .odl.in files.
# It temporarily replaces ${} to avoid its parsing by `configure_file`.
function(configure_file_odl odl_file_input)
    get_filename_component(input_directory ${odl_file_input} DIRECTORY)
    get_filename_component(input_filename ${odl_file_input} NAME)

    string(REGEX REPLACE "\\.[^.]*$" "" barename ${input_filename})
    file(RELATIVE_PATH relative_path ${CMAKE_CURRENT_SOURCE_DIR} ${input_directory})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${relative_path}")

    # Replace ${} with $@@{} to prevent parsing by `configure_file`.
    execute_process(COMMAND bash "-c" "sed -r -i 's/\\$\\{([^}]*)\\}/\\$@@\\{\\1\\}/g' ${odl_file_input}")

    message(STATUS "Generating ODL file: ${barename}")
    configure_file(${odl_file_input} "${CMAKE_CURRENT_BINARY_DIR}/${relative_path}/${barename}")

    # Revert $@@{} back to ${} after `configure_file`.
    execute_process(COMMAND bash "-c" "sed -r -i 's/\\$@@\\{([^}]*)\\}/\\$\\{\\1\\}/g' ${CMAKE_CURRENT_BINARY_DIR}/${relative_path}/${barename}")
endfunction()
