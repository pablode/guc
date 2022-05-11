# cgltf
add_library(cgltf INTERFACE)
set_target_properties(cgltf PROPERTIES LINKER_LANGUAGE C)
target_include_directories(cgltf INTERFACE cgltf)

# MikkTSpace
add_library(MikkTSpace MikkTSpace/mikktspace.c MikkTSpace/mikktspace.h)
target_include_directories(MikkTSpace PUBLIC MikkTSpace)
set_target_properties(
  MikkTSpace
  PROPERTIES
    LINKER_LANGUAGE C
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
