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
)

set_target_properties(
  guc
  PROPERTIES
    # Search libguc in executable dir on MacOS
    BUILD_RPATH_USE_ORIGIN ON
)

install(
  TARGETS guc
  COMPONENT guc
)
