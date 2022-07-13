add_library(
  libguc
  include/guc.h
  src/guc.cpp
  src/cgltf_util.h
  src/cgltf_util.cpp
  src/usdpreviewsurface.h
  src/usdpreviewsurface.cpp
  src/materialx.h
  src/materialx.cpp
  src/mesh.h
  src/mesh.cpp
  src/naming.h
  src/naming.cpp
  src/converter.h
  src/converter.cpp
  src/debugCodes.h
  src/debugCodes.cpp
  src/image.h
  src/image.cpp
)

target_link_libraries(
  libguc
  PRIVATE
    cgltf
    MikkTSpace
    usd
    usdGeom
    usdLux
    usdShade
    usdUtils
    usdMtlx
    MaterialXCore
    MaterialXFormat
    OpenImageIO::OpenImageIO
)

target_include_directories(
  libguc
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    src
)

target_compile_definitions(
  libguc
  PRIVATE
    # Workaround for https://github.com/PixarAnimationStudios/USD/issues/1471#issuecomment-799813477
    "$<$<OR:$<CONFIG:>,$<CONFIG:Debug>>:TBB_USE_DEBUG>"
)

set_target_properties(
  libguc
  PROPERTIES
    # We don't want 'liblibguc' on Linux/Unix
    PREFIX ""
)

if(WIN32 AND BUILD_SHARED_LIBS)
  set_target_properties(libguc PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()

install(
  TARGETS libguc
  COMPONENT libguc
)
