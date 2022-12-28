#
# In this CMakeLists.txt file, two targets are defined:
# 1. libguc, a library with a USD-independent C-API
# 2. usdGlTF, the USD SDF plugin that gets installed as a DLL only
#
# Both projects share most source files and dependencies. They differ in some
# additionally included file, their build config, installation properties and
# whether a header include dir is installed, or not.
#

set(LIBGUC_SHARED_SRCS
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

set(LIBGUC_SHARED_LIBRARIES
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

set(LIBGUC_VERSION_DEFINES
  GUC_VERSION_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR}
  GUC_VERSION_MINOR=${CMAKE_PROJECT_VERSION_MINOR}
  GUC_VERSION_PATCH=${CMAKE_PROJECT_VERSION_PATCH}
  GUC_VERSION_STRING="${CMAKE_PROJECT_VERSION}"
)

add_library(
  libguc
  ${LIBGUC_SHARED_SRCS}
  include/guc_params.h
  include/guc.h
  src/guc.cpp
)

target_link_libraries(libguc PRIVATE ${LIBGUC_SHARED_LIBRARIES})
target_include_directories(libguc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_definitions(
  libguc
  PUBLIC
    ${LIBGUC_VERSION_DEFINES}
  PRIVATE
    # Workaround for https://github.com/PixarAnimationStudios/USD/issues/1471#issuecomment-799813477
    "$<$<OR:$<CONFIG:>,$<CONFIG:Debug>>:TBB_USE_DEBUG>"
)

# We don't want 'liblibguc' on Linux/Unix
set_target_properties(libguc PROPERTIES PREFIX "")

if(WIN32 AND BUILD_SHARED_LIBS)
  set_target_properties(libguc PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()

# Need fPIC for static linking because the USD libraries we use are built with it
set_target_properties(libguc PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS libguc COMPONENT libguc)

if(GUC_BUILD_USDGLTF)
  add_library(
    usdGlTF SHARED
    ${LIBGUC_SHARED_SRCS}
    include/guc_params.h
    src/fileFormat.h
    src/fileFormat.cpp
  )

  target_link_libraries(
    usdGlTF
    PUBLIC
      sdf
    PRIVATE
      ${LIBGUC_SHARED_LIBRARIES}
  )

  target_include_directories(usdGlTF PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

  target_compile_definitions(
    usdGlTF
    PRIVATE
      ${LIBGUC_VERSION_DEFINES}
      # Workaround for https://github.com/PixarAnimationStudios/USD/issues/1471#issuecomment-799813477
      "$<$<OR:$<CONFIG:>,$<CONFIG:Debug>>:TBB_USE_DEBUG>"
  )

  # We don't want 'liblibguc' on Linux/Unix
  set_target_properties(usdGlTF PROPERTIES PREFIX "")

  # Is installed to "plugins/usd" dir; remove the default "lib" folder prefix.
  install(TARGETS usdGlTF COMPONENT usdGlTF DESTINATION ".")

  set(PLUGINFO_PATH "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")
  set(PLUG_INFO_LIBRARY_PATH "../usdGlTF${CMAKE_SHARED_LIBRARY_SUFFIX}")
  configure_file(plugInfo.json.in "${PLUGINFO_PATH}" @ONLY)

  install(
    FILES "${PLUGINFO_PATH}"
    DESTINATION "usdGlTF/resources"
    COMPONENT usdGlTF
  )
endif()
