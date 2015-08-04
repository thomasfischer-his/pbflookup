project(pbflookup)
cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})

find_package(Protobuf REQUIRED)

find_package(ZLIB REQUIRED)

find_package(Osmpbf REQUIRED)

include_directories(
  ${PROTOBUF_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  ${OSMPBF_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
  ${PROTOBUF_LITE_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${OSMPBF_LIBRARIES}
)

