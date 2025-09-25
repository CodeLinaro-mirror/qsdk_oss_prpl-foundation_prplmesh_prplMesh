set(BEEROCKS_CONTROLLER_DATAELEMENTS_VAP_CONFIGS 0)
if (NOT USE_PRPLMESH_WHM)
        set(BEEROCKS_CONTROLLER_DATAELEMENTS_VAP_CONFIGS 1)
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/beerocks_controller.conf.in"
        "${CMAKE_CURRENT_BINARY_DIR}/beerocks_controller.conf"
        )

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/beerocks_controller.conf DESTINATION config)
file(COPY ${CMAKE_CURRENT_BINARY_DIR}/beerocks_controller.conf DESTINATION "${CMAKE_MULTIAP_OUTPUT_DIRECTORY}/config")
