project(beerocks_vendor_message VERSION ${prplmesh_VERSION})
message("${BoldWhite}Preparing ${BoldGreen}${PROJECT_NAME}${BoldWhite} for the ${BoldGreen}${TARGET_PLATFORM}${BoldWhite} platform${ColourReset}")

# Set the base path for the current module
set(MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

file(GLOB beerocks_vendor_message_sources
    ${MODULE_PATH}/src/*.c*
    ${MODULE_PATH}/../agent/src/beerocks/slave/agent_db.c*
)

if (USE_PRPLMESH_WHM)
    set(INCLUDE_DIR 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/airties/>
    )
    list(APPEND beerocks_vendor_message_sources
        ${MODULE_PATH}/airties/airties_utils.cpp
        ${MODULE_PATH}/common/common_utility.cpp
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_PRPLMESH_WHM ${WBAPI_SOCK}")
    list(APPEND LIBS wbapi) 
else()
    list(APPEND beerocks_vendor_message_sources
        ${MODULE_PATH}/airties/dummy/airties_utils_dummy.cpp
        ${MODULE_PATH}/dummy/common_utility_dummy.cpp
    )
endif()

add_executable(${PROJECT_NAME} ${beerocks_vendor_message_sources})
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-z,defs")
target_link_libraries(${PROJECT_NAME} bcl btl tlvf btlvf bwl bpl nbapi ${LIBS})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/> 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common/> 
        ${INCLUDE_DIR}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../agent/src/beerocks/slave/>
    )

# Install
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
