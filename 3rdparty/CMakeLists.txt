if(NOT UNIX)
    message(FATAL_ERROR "最新の依存関係ビルドはmacOSとFreeBSDを対象にしています。")
endif()

# Build the bundled dependencies as CMake targets so their generated headers
# and static libraries are consumed directly by AACGain.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FAAD_BUNDLED_MODE ON CACHE BOOL "" FORCE)
set(FAAD_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(faad2 faad2-build EXCLUDE_FROM_ALL)

set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(mp4v2 mp4v2-build EXCLUDE_FROM_ALL)

add_library(faad2 ALIAS faad)
