# 测试目标：greyfall_tests（主测试套件）+ greyfall_bots（平衡脚本）
# 主套件用单翻译单元 + 静态注册，避免为每个用例建独立可执行文件的开销。

add_executable(greyfall_tests EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(greyfall_tests PRIVATE greyfall_core)
target_compile_definitions(greyfall_tests PRIVATE GF_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(greyfall_bots EXCLUDE_FROM_ALL bots.cpp)
target_link_libraries(greyfall_bots PRIVATE greyfall_core)

# 并发原子写回归守卫（多进程写同一路径）。
# 该工具用 fork/wait 制造多进程竞争，是 POSIX 专有的 —— 交叉编译到 Windows 时跳过。
if(NOT WIN32)
  add_executable(greyfall_race EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/tools/race_atomic_write.cpp")
  target_link_libraries(greyfall_race PRIVATE greyfall_core)
endif()

# 让默认构建同时产出测试可执行文件
if(WIN32)
  add_custom_target(greyfall_tests_build ALL DEPENDS greyfall_tests greyfall_bots)
else()
  add_custom_target(greyfall_tests_build ALL DEPENDS greyfall_tests greyfall_bots greyfall_race)
endif()

# 每个用例使用独立工作目录：并发执行时多个进程不会竞争同一个 test-data 前缀，
# 也不会互相看到对方的结果文件（此前这是并行 CTest 下偶发失败的来源）。
set(GF_SUITES tickorder crypto rng fixed save market ai items clue gen replay cli resolution victory casus personnel gov design starbase revolt species construction negotiate combat techpolicy front peace parliament trade commander spy mechanics development)
foreach(suite IN LISTS GF_SUITES)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-${suite}")
  add_test(NAME ${suite} COMMAND greyfall_tests ${suite})
  set_tests_properties(${suite} PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-${suite}")
endforeach()

# selftest / bots / race 的工作目录需在配置期存在（CTest 不会自动创建）
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-selftest")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-bots")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-race")

# selftest：内置 85 项断言 + 200 tick 自动对局
add_test(NAME selftest COMMAND greyfall selftest)
set_tests_properties(selftest PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-selftest")

# 平衡脚本（50 纪元 × 3 策略）；可用 GREYFALL_BOT_EPOCHS 缩小规模
add_test(NAME bots COMMAND greyfall_bots)
set_tests_properties(bots PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-bots" TIMEOUT 1800)

# 并发原子写：16 进程 × 60 次写同一路径，必须零失败且最终文件完整
# 多进程竞争测试仅适用于 POSIX（Windows 上无 fork）
if(NOT WIN32)
  add_test(NAME race_atomic_write COMMAND greyfall_race 16 60)
  set_tests_properties(race_atomic_write PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-race" TIMEOUT 300)
endif()

# 端到端 CLI 事务：new → order → advance → verify
add_test(NAME e2e_cli
  COMMAND ${CMAKE_COMMAND}
    -DGREYFALL=$<TARGET_FILE:greyfall>
    -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/e2e
    -P "${CMAKE_CURRENT_SOURCE_DIR}/e2e_cli.cmake")
set_tests_properties(e2e_cli PROPERTIES TIMEOUT 300)
