# cgltf
add_library(cgltf INTERFACE)
set_target_properties(cgltf PROPERTIES LINKER_LANGUAGE C)
target_include_directories(cgltf INTERFACE cgltf)

# MikkTSpace
add_library(MikkTSpace STATIC MikkTSpace/mikktspace.c MikkTSpace/mikktspace.h)
set_target_properties(MikkTSpace PROPERTIES LINKER_LANGUAGE C)
target_include_directories(MikkTSpace
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/MikkTSpace>
  PUBLIC $<INSTALL_INTERFACE:>
)
