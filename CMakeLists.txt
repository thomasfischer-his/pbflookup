project(pbflookup)
cmake_minimum_required(VERSION 2.8)
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})

# Requre C++-11 support
set(CMAKE_CXX_FLAGS "-Wall -std=c++11")

# Use GProf for profiling
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
#SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
#SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")

add_definitions(-DDEBUG)
add_definitions(-DCPUTIMER)
add_definitions(-DREVERSE_ID_TREE)
add_definitions(-DLATEX_OUTPUT)

find_package(PkgConfig REQUIRED)

find_package(Protobuf REQUIRED)

find_package(ZLIB REQUIRED)

find_package(Osmpbf REQUIRED)

find_package(Boost REQUIRED
  COMPONENTS iostreams system thread
)

pkg_check_modules(LIBCONFIG REQUIRED
  libconfig++>=1.5
)

include_directories(
  ${PROTOBUF_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  ${OSMPBF_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
  ${LIBCONFIG_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
  ${PROTOBUF_LITE_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${OSMPBF_LIBRARIES}
  ${Boost_LIBRARIES}
  ${LIBCONFIG_LIBRARIES}
)

install(TARGETS
  pbflookup
  RUNTIME
  DESTINATION bin
)

add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_INSTALL_PREFIX}/bin/pbflookup"
)
