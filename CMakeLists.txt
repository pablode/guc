cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

project(guc VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find USD - only non-monolithic builds have been tested.
find_package(pxr CONFIG REQUIRED)

# Find MaterialX library provided by the USD installation.
# Since we use UsdMtlx, using a custom MaterialX version leads to conflicts.
find_package(MaterialX REQUIRED HINTS ${pxr_DIR})

# We need to open PNG and JPEG files in order to read the number of channels
# for shading node creation. OIIO should be provided by the USD installation.
find_package(OpenImageIO REQUIRED HINTS ${pxr_DIR})

option(GUC_BUILD_EXECUTABLE "Build the guc executable." ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE} ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endforeach()

add_subdirectory(extern)

include(cmake/UsdDefaults.cmake)
set(CMAKE_CXX_FLAGS "${GUC_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")
add_definitions(${GUC_CXX_DEFINITIONS})

add_subdirectory(src)
