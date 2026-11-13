add_executable(bpl_test bpl_test.cpp)
target_link_libraries(bpl_test dl bpl mapfcommon elpp bcl)
target_include_directories(bpl_test PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
	)

if(USE_PRPLMESH_WHM)
    add_executable(bpl_dhcp_dm_test bpl_dhcp_dm_test.cpp ../dhcp/dm/bpl_dhcp_utils.cpp)
    target_include_directories(bpl_dhcp_dm_test PRIVATE
        ../include ../dhcp/dm
        ${AMXB_INCLUDE_DIRS} ${AMXC_INCLUDE_DIRS} ${AMXD_INCLUDE_DIRS} ${AMXP_INCLUDE_DIRS}
    )
    target_link_libraries(bpl_dhcp_dm_test PRIVATE wbapi bcl tlvf elpp amxc gtest_main)
    add_test(NAME bpl_dhcp_dm_test COMMAND $<TARGET_FILE:bpl_dhcp_dm_test>)
endif()
