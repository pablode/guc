#
# Finds draco and populates following variables:
#   DRACO_FOUND (draco_FOUND)
#   DRACO_INCLUDE_DIR
#   DRACO_LIBRARIES
#
# The reason why we ship our custom Find* file is that the draco version
# that USD uses, 1.3.6, has a broken CMake config file.
#
# This also caused trouble for USD, which is why it comes with a custom
# Find* script similar to this one. However, the script has two issues:
#   1) It specifies full file names, causing 'libdraco.1.dylib' to not
#      be found on macOS.
#   2) It only finds the main draco lib, but not the decoder-specific
#      lib which we need in guc.
#

find_path(DRACO_INCLUDE_DIR NAMES "draco/core/draco_version.h")

find_library(DRACO_LIBRARY NAMES draco PATH_SUFFIXES lib)
find_library(DRACO_DEC_LIBRARY NAMES dracodec PATH_SUFFIXES lib)

set(DRACO_LIBRARIES ${DRACO_LIBRARY} ${DRACO_DEC_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(draco
  REQUIRED_VARS
    DRACO_INCLUDE_DIR
    DRACO_DEC_LIBRARY
    DRACO_LIBRARY
    DRACO_LIBRARIES
)
