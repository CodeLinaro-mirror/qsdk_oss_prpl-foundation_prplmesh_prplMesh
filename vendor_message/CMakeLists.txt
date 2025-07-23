project(beerocks_vendor_message VERSION ${prplmesh_VERSION})
message("${BoldWhite}Preparing ${BoldGreen}${PROJECT_NAME}${BoldWhite} for the ${BoldGreen}${TARGET_PLATFORM}${BoldWhite} platform${ColourReset}")

# Set the base path for the current module
set(MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

if (USE_PRPLMESH_WHM)
    file(GLOB beerocks_vendor_message_sources
        ${MODULE_PATH}/src/*.c*
        ${MODULE_PATH}/../agent/src/beerocks/slave/agent_db.c*
    )
    list(APPEND beerocks_vendor_message_sources
        ${MODULE_PATH}/airties/airties_utils.cpp
        ${MODULE_PATH}/common/common_utility.cpp
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_PRPLMESH_WHM ${WBAPI_SOCK}")
    list(APPEND LIBS wbapi) 
else()
    file(GLOB beerocks_vendor_message_sources
        ${MODULE_PATH}/dummy/*.c*
        ${MODULE_PATH}/../agent/src/beerocks/slave/agent_db.c*
    )
    list(APPEND beerocks_vendor_message_sources
        ${MODULE_PATH}/airties/dummy/airties_utils_dummy.cpp
    )
endif()

add_executable(${PROJECT_NAME} ${beerocks_vendor_message_sources})
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-z,defs")
target_link_libraries(${PROJECT_NAME} bcl btl tlvf btlvf bwl bpl nbapi ${LIBS})

if (USE_PRPLMESH_WHM)
    set(INCLUDE_DIR
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/airties/>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>  # Include this only for WHM
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common/>
    )
else()
    set(INCLUDE_DIR
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dummy/>    # Optional: dummy headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/airties/dummy/>    # Optional: dummy headers
    )
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../agent/src/beerocks/slave/>
        ${INCLUDE_DIR}
    )

# Install
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
