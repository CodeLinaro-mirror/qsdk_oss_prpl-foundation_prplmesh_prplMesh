# Build Beerocks Modules
if (PRPLMESH_BML)
    add_subdirectory("bml")
endif()
if (PRPLMESH_BEEROCKS_CLI)
    add_subdirectory("cli")
endif()
add_subdirectory("master")

if (ENABLE_NBAPI AND PRPLMESH_CLI)
    add_subdirectory("prplmesh-cli")
endif()
