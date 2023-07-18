cmake_minimum_required(VERSION 3.0.0)
project(lumidb VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-std=c++17")

option(lumidb_test "enable test" on)

set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# third-party options
set(ARGUMENTUM_BUILD_STATIC_LIBS on)

# third-party source files
add_subdirectory(third-party/argumentum)
add_subdirectory(third-party/backward-cpp)
add_subdirectory(third-party/fmt)
add_subdirectory(third-party/tabulate)


## special handling for isocline
add_library(isocline STATIC third-party/isocline/src/isocline.c)

# add include files
set(lumidb_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/third-party/argumentum/include
  ${CMAKE_SOURCE_DIR}/third-party/fmt/include
  ${CMAKE_SOURCE_DIR}/third-party/tabulate/include
  ${CMAKE_SOURCE_DIR}/third-party/acutest/include
  ${CMAKE_SOURCE_DIR}/third-party/isocline/include
)

# lumidb main lib
add_subdirectory(src/lumidb)

# plugins
add_subdirectory(src/plugins)

add_executable(lumidb main.cc ${BACKWARD_ENABLE})
target_link_libraries(lumidb lumidb-lib fmt::fmt Argumentum::argumentum isocline)

target_include_directories(lumidb PUBLIC ${lumidb_INCLUDE_DIRS})
target_include_directories(lumidb-lib PUBLIC ${lumidb_INCLUDE_DIRS})

add_backward(lumidb-lib)
add_backward(lumidb)

if(lumidb_test)
  enable_testing()
  add_subdirectory(test)
endif(lumidb_test)
