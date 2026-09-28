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
# 发布版（-DGREYFALL_RELEASE=ON）：屏蔽面向调试/开发的命令
#
# 被屏蔽的是：logs / history / replay / rollback / chronicle。
# 它们让玩家能**查看历史状态与事件全量记录、并把存档回退到任意时点** ——
# 对一款以「信息不对称 + 不可逆抉择 + AI 会读取你的读档痕迹」为核心机制的作品，
# 这些命令直接摧毁博弈前提（AI 的背叛 EV 计算里就有 rollbackCount 这一项）。
#
# 屏蔽发生在**命令表**层面：help / man / 分发都从同一张表读取，
# 因此一处生效，且 `man logs` 也会如实报告「该命令不可用」。
# 数据层（history 数组、chronicle 链文件）不受影响，开发版仍可用。
# ---------------------------------------------------------------------------
option(GREYFALL_RELEASE "构建发布版：屏蔽调试命令（logs/history/replay/rollback/chronicle）" OFF)
if(GREYFALL_RELEASE)
  target_compile_definitions(greyfall_core PUBLIC GF_RELEASE_BUILD=1)
  message(STATUS "GREYFALL_RELEASE=ON —— 发布版：已屏蔽 logs/history/replay/rollback/chronicle")
endif()

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
