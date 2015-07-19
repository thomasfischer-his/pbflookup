project(pbflookup)
cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})

find_library(PROTOBUF_LITE_LIBRARY protobuf-lite)
find_package(ZLIB REQUIRED)

find_path(OSMBINARY_INCLUDE_DIR osmpbf/osmpbf.h)

find_library(OSMBINARY_LIBRARIES NAMES libosmpbf.a)

include_directories(
  ${PROTOBUF_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  ${OSMBINARY_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}
  ${PROTOBUF_LIBRARY}
  ${ZLIB_LIBRARIES}
  ${OSMBINARY_LIBRARIES}
)

