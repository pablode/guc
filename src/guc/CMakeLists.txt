add_executable(guc main.c)

set_target_properties(
  guc
  PROPERTIES
    LINKER_LANGUAGE C
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_link_libraries(
  guc
  PRIVATE
    libguc
    cargs
)

set_target_properties(
  guc
  PROPERTIES
    # Search libguc in executable dir on MacOS
    BUILD_RPATH_USE_ORIGIN ON
)

# Generate C header file with a string that contains the license text
file(READ "${PROJECT_SOURCE_DIR}/LICENSE" LICENSE_TEXT)
string(REGEX REPLACE "\"" "\\\\\"" LICENSE_TEXT "${LICENSE_TEXT}")
# Instead of concatenating a single string with backslashes, we concatenate
# multiple strings in order to prevent MSVC from throwing C2026: "string too big".
# This is why we manually escape quotation marks and do not use configure_file's
# ESCAPE_QUOTES argument.
string(REGEX REPLACE "\n" "\\\\n\" \\\\\\n \"" LICENSE_TEXT "${LICENSE_TEXT}")
configure_file(license.h.in license.h @ONLY)

target_include_directories(
  guc
  PRIVATE
    # Allow discovery of generated license.h in build dir
    ${CMAKE_CURRENT_BINARY_DIR}
)

install(
  TARGETS guc
  COMPONENT guc
)
