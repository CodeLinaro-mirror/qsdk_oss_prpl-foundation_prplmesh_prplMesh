# Build Beerocks Modules
add_subdirectory("bml")
if (ENABLE_BEEROCKS_CLI)
    add_subdirectory("cli")
endif()
add_subdirectory("master")

if (ENABLE_NBAPI AND ENABLE_PRPLMESH_CLI)
    add_subdirectory("prplmesh-cli")
endif()
