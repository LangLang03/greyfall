# 端到端 CLI 事务测试：一次进程调用 = 一个事务，无 REPL
# 覆盖：new → status → order → advance（含退出码 5）→ choose → verify → slots → export/import
#        → rollback（含 chronicle 记录）→ replay → epoch --report

if(NOT DEFINED GREYFALL)
  message(FATAL_ERROR "需要 -DGREYFALL=<greyfall 可执行文件路径>")
endif()
if(NOT DEFINED WORKDIR)
  message(FATAL_ERROR "需要 -DWORKDIR=<工作目录>")
endif()

file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")

set(ENV{GREYFALL_DIR} "${WORKDIR}/data")

function(gf_run expect_code)
  execute_process(
    COMMAND ${GREYFALL} ${ARGN}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    WORKING_DIRECTORY "${WORKDIR}")
  if(NOT code EQUAL expect_code)
    message(FATAL_ERROR "`greyfall ${ARGN}` 退出码 ${code}，期望 ${expect_code}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  set(GF_LAST_OUT "${out}" PARENT_SCOPE)
endfunction()

# 1) 开新纪元
gf_run(0 new --seed 5EED-C0FFEE --difficulty 3 --empires 12)
if(NOT GF_LAST_OUT MATCHES "新纪元已开启")
  message(FATAL_ERROR "new 未输出预期内容：${GF_LAST_OUT}")
endif()

# 2) 无存档槽 ⇒ 退出码 2
gf_run(2 --slot nosuch status)

# 3) 状态总览
gf_run(0 status)
if(NOT GF_LAST_OUT MATCHES "灰域纪元")
  message(FATAL_ERROR "status 输出异常")
endif()

# 4) 下单（限价，可能全部或部分成交）
gf_run(0 order buy alloys 1200 @33.0 --tif gtc)

# 5) 推进 3 季（若出现待抉择则退出码 5）
# dry-run 必须同时保持存档与 chronicle 不变。
file(GLOB_RECURSE dry_files "${WORKDIR}/data/*")
foreach(path IN LISTS dry_files)
  file(SHA256 "${path}" before_hash)
  string(SHA256 key "${path}")
  set("dry_before_${key}" "${before_hash}")
endforeach()
execute_process(COMMAND ${GREYFALL} advance --ticks 1 --dry-run
  RESULT_VARIABLE dry_code OUTPUT_QUIET ERROR_QUIET WORKING_DIRECTORY "${WORKDIR}")
if(NOT dry_code EQUAL 0 AND NOT dry_code EQUAL 5)
  message(FATAL_ERROR "dry-run 退出码 ${dry_code}")
endif()
file(GLOB_RECURSE dry_after_files "${WORKDIR}/data/*")
if(NOT "${dry_files}" STREQUAL "${dry_after_files}")
  message(FATAL_ERROR "dry-run 创建或删除了文件")
endif()
foreach(path IN LISTS dry_files)
  file(SHA256 "${path}" after_hash)
  string(SHA256 key "${path}")
  if(NOT "${dry_before_${key}}" STREQUAL "${after_hash}")
    message(FATAL_ERROR "dry-run 修改了 ${path}")
  endif()
endforeach()
execute_process(
  COMMAND ${GREYFALL} advance --ticks 3
  RESULT_VARIABLE adv_code
  OUTPUT_VARIABLE adv_out
  WORKING_DIRECTORY "${WORKDIR}")
if(NOT adv_code EQUAL 0 AND NOT adv_code EQUAL 5)
  message(FATAL_ERROR "advance 退出码 ${adv_code}\n${adv_out}")
endif()

# 6) 有抉择就结算掉
foreach(i RANGE 1 8)
  execute_process(COMMAND ${GREYFALL} choose 0 0
    RESULT_VARIABLE c RESULT_VARIABLE c OUTPUT_QUIET WORKING_DIRECTORY "${WORKDIR}")
  if(NOT c EQUAL 0)
    break()
  endif()
endforeach()

# 7) 存档校验
gf_run(0 verify main)
if(NOT GF_LAST_OUT MATCHES "通过")
  message(FATAL_ERROR "verify 未通过：${GF_LAST_OUT}")
endif()

# 8) 槽列表
gf_run(0 slots)
if(NOT GF_LAST_OUT MATCHES "main")
  message(FATAL_ERROR "slots 未列出 main")
endif()

# 9) 参数错误 ⇒ 退出码 1
gf_run(1 advance --ticks 0)
gf_run(1 order buy no-such-resource 10 @1.0)

# 10) 未知命令 ⇒ 退出码 1
gf_run(1 frobnicate)

# 11) 导出 / 导入
gf_run(0 export main "${WORKDIR}/backup.gsv")
if(NOT EXISTS "${WORKDIR}/backup.gsv")
  message(FATAL_ERROR "export 未生成文件")
endif()
gf_run(0 import "${WORKDIR}/backup.gsv" restored)
gf_run(0 --slot restored status)

# 12) chronicle 链完整
gf_run(0 chronicle)
if(NOT GF_LAST_OUT MATCHES "完整")
  message(FATAL_ERROR "chronicle 链校验失败：${GF_LAST_OUT}")
endif()

# 13) 回退（写 rollbackCount）
execute_process(COMMAND ${GREYFALL} rollback 2
  RESULT_VARIABLE rb RESULT_VARIABLE rb OUTPUT_VARIABLE rb_out WORKING_DIRECTORY "${WORKDIR}")
if(NOT rb EQUAL 0 AND NOT rb EQUAL 4)
  message(FATAL_ERROR "rollback 退出码 ${rb}\n${rb_out}")
endif()

# 14) 纪元报告
gf_run(0 epoch --report)
if(NOT GF_LAST_OUT MATCHES "结局向量")
  message(FATAL_ERROR "epoch --report 输出异常")
endif()

# 15) 手册与版本
gf_run(0 man advance)
gf_run(0 version)
gf_run(0 help --short)

# 16) 缺参数必须报错而不是崩溃
gf_run(1 order)
gf_run(1 book)
gf_run(1 envoy)

message(STATUS "e2e_cli 全部通过")
