cmake_minimum_required(VERSION 3.10)
project(PlotFS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "-D_FILE_OFFSET_BITS=64")
set(CMAKE_CXX_FLAGS ${CMAKE_C_FLAGS})

find_package(PkgConfig REQUIRED) 
find_library(FUSE3_LIBRARY fuse3)
find_package_handle_standard_args(FUSE3 REQUIRED_VARS FUSE3_LIBRARY VERSION_VAR FUSE3_VERSION_STRING)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/plotfs_generated.h
  COMMAND flatc --cpp --gen-object-api -o ${CMAKE_CURRENT_BINARY_DIR}/ plotfs.fbs
  DEPENDS  plotfs.fbs
)

add_executable(plotfs plotfs_generated.h cli.cpp)
add_executable(mount.plotfs plotfs_generated.h mount.cpp)
target_link_libraries(mount.plotfs ${FUSE3_LIBRARY})

install(TARGETS plotfs mount.plotfs DESTINATION bin)