cmake_minimum_required(VERSION 3.28)
project(greyfall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wshadow)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O2)
endif()

# ---------------------------------------------------------------------------
# 核心静态库：除 src/main.cpp 外全部翻译单元
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GF_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER GF_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")

add_library(greyfall_core STATIC ${GF_SOURCES})
target_include_directories(greyfall_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(greyfall_core PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# 主可执行文件
# ---------------------------------------------------------------------------
add_executable(greyfall "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(greyfall PRIVATE greyfall_core)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# selftest target：直接跑内置自检 + 固定种子自动对局
# ---------------------------------------------------------------------------
add_custom_target(selftest
  COMMAND $<TARGET_FILE:greyfall> selftest --verbose
  DEPENDS greyfall
  COMMENT "greyfall 内置自检（约 40 项断言 + 200 tick 自动对局）"
  VERBATIM)
