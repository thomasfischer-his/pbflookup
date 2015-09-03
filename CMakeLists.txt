project(pbflookup)
cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})

# Requre C++-11 support
set(CMAKE_CXX_FLAGS "-Wall -std=c++11")

add_definitions(-DDEBUG)
add_definitions(-DREVERSE_ID_TREE)

find_package(Protobuf REQUIRED)

find_package(ZLIB REQUIRED)

find_package(Osmpbf REQUIRED)

find_package(PkgConfig)
pkg_check_modules(LIBLZMA REQUIRED liblzma>=5.0.8)

include_directories(
  ${PROTOBUF_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  ${OSMPBF_INCLUDE_DIRS}
  ${LIBLZMA_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
  ${PROTOBUF_LITE_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${OSMPBF_LIBRARIES}
  ${LIBLZMA_LIBRARIES}
)

