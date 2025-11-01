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

set(LIBGUC_PUBLIC_HEADERS
  include/guc.h
)

set(LIBGUC_DEFINES
  GUC_VERSION_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR}
  GUC_VERSION_MINOR=${CMAKE_PROJECT_VERSION_MINOR}
  GUC_VERSION_PATCH=${CMAKE_PROJECT_VERSION_PATCH}
  GUC_VERSION_STRING="${CMAKE_PROJECT_VERSION}"
)

if(TARGET usd_ms)
  set(LIBGUC_USD_LIBS usd_ms)
else()
  set(LIBGUC_USD_LIBS usd usdGeom usdLux usdShade usdUtils usdMtlx usdUI)
endif()

set(LIBGUC_SHARED_LIBRARIES
  # Header-only library without link dependencies --
  # we can get away with not installing the target.
  $<BUILD_INTERFACE:cgltf>
  MikkTSpace
  MaterialXCore
  MaterialXFormat
  meshoptimizer
  ${LIBGUC_USD_LIBS}
)

if(OpenImageIO_FOUND)
  list(APPEND LIBGUC_DEFINES "GUC_USE_OIIO")
  list(APPEND LIBGUC_SHARED_LIBRARIES OpenImageIO::OpenImageIO)
else()
  list(APPEND LIBGUC_SHARED_LIBRARIES $<BUILD_INTERFACE:stb_image>)
endif()

if(draco_FOUND)
  list(APPEND LIBGUC_DEFINES "GUC_USE_DRACO")
  list(APPEND LIBGUC_SHARED_LIBRARIES ${DRACO_LIBRARIES})
endif()

#
# libguc
#
add_library(
  libguc
  ${LIBGUC_SHARED_SRCS}
  ${LIBGUC_PUBLIC_HEADERS}
  src/guc.cpp
)

target_link_libraries(libguc PRIVATE ${LIBGUC_SHARED_LIBRARIES})

target_include_directories(libguc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(draco_FOUND)
  target_include_directories(libguc PRIVATE ${DRACO_INCLUDE_DIRS})
endif()

set_target_properties(
  libguc
  PROPERTIES
    PUBLIC_HEADER "${LIBGUC_PUBLIC_HEADERS}"
    # We don't want 'liblibguc' on Linux/Unix
    PREFIX ""
    # fPIC for static linking because the USD libraries we use are built with it
    POSITION_INDEPENDENT_CODE ON
)

if(WIN32 AND BUILD_SHARED_LIBS)
  set_target_properties(libguc PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()

target_compile_definitions(
  libguc
  PUBLIC
    ${LIBGUC_DEFINES}
  PRIVATE
    # Workaround for https://github.com/PixarAnimationStudios/USD/issues/1471#issuecomment-799813477
    "$<$<OR:$<CONFIG:>,$<CONFIG:Debug>>:TBB_USE_DEBUG>"
)

# Installation and cmake config file
install(TARGETS libguc MikkTSpace EXPORT gucTargets COMPONENT libguc)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    gucConfigVersion.cmake
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion
)
install(EXPORT gucTargets
    FILE gucTargets.cmake
    DESTINATION lib/cmake/guc
    COMPONENT libguc
)

configure_file("${PROJECT_SOURCE_DIR}/cmake/gucConfig.cmake.in" gucConfig.cmake @ONLY)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/gucConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/gucConfigVersion.cmake"
  DESTINATION lib/cmake/guc
  COMPONENT libguc
)

#
# usdGlTF
#
if(GUC_BUILD_USDGLTF)
  add_library(
    usdGlTF SHARED
    ${LIBGUC_SHARED_SRCS}
    src/fileFormat.h
    src/fileFormat.cpp
  )

  target_link_libraries(
    usdGlTF
    PRIVATE
      ${LIBGUC_SHARED_LIBRARIES}
  )

  if(TARGET usd_ms)
    set(LIBGUC_SDF_LIB usd_ms)
  else()
    set(LIBGUC_SDF_LIB sdf)
  endif()

  target_link_libraries(
    usdGlTF
    PUBLIC
      ${LIBGUC_SDF_LIB}
  )

  target_include_directories(usdGlTF PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

  if(draco_FOUND)
    target_include_directories(usdGlTF PRIVATE ${DRACO_INCLUDE_DIRS})
  endif()

  target_compile_definitions(
    usdGlTF
    PRIVATE
      ${LIBGUC_DEFINES}
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
