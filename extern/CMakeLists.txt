add_library(cgltf INTERFACE)
set_target_properties(cgltf PROPERTIES LINKER_LANGUAGE C)
target_include_directories(cgltf INTERFACE cgltf)
