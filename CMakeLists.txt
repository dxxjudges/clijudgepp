cmake_minimum_required(VERSION 3.16)
project(CliJudgePP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

find_package(Threads REQUIRED)

# ── 复用 clijudge 主项目 include（单一数据源，不复制副本） ─────────────
# 查找顺序:
#   1. 显式传入的 CLIJUDGE_INCLUDE_DIR
#   2. 仓库内 include（可选，用于子模块/镜像布局）
#   3. 仓库内 clijudge/include（CI 检出到工作区子目录）
#   4. ../../main/include（本机 judgelite 布局: gui/main 与 main 平级）
#   5. ../clijudge/include、../main/include（与主项目同目录的部署布局）
if(NOT CLIJUDGE_INCLUDE_DIR)
  foreach(_cand
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/clijudge/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../main/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/../clijudge/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/../main/include")
    if(EXISTS "${_cand}/platform.h" AND EXISTS "${_cand}/problem.h")
      set(CLIJUDGE_INCLUDE_DIR "${_cand}")
      break()
    endif()
  endforeach()
endif()

if(NOT EXISTS "${CLIJUDGE_INCLUDE_DIR}/platform.h")
  message(FATAL_ERROR
    "clijudge headers not found. Set -DCLIJUDGE_INCLUDE_DIR=/path/to/clijudge/include "
    "(the GUI shares the CLI data model and judge implementation).")
endif()
message(STATUS "Using clijudge headers: ${CLIJUDGE_INCLUDE_DIR}")

# ── 前端资源内嵌（单文件分发的 GUI 无需额外 web/ 目录） ─────────
file(GLOB WEB_ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
set(WEB_ASSETS_HPP "${CMAKE_CURRENT_BINARY_DIR}/web_assets.hpp")
add_custom_command(
  OUTPUT "${WEB_ASSETS_HPP}"
  COMMAND "${CMAKE_COMMAND}"
          "-DASSETS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/assets"
          "-DOUTPUT_FILE=${WEB_ASSETS_HPP}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake"
  DEPENDS ${WEB_ASSET_FILES}
          "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake"
  COMMENT "Embedding web assets"
  VERBATIM)
add_custom_target(clijudgepp_assets DEPENDS "${WEB_ASSETS_HPP}")

add_executable(clijudgepp src/gui_main.cpp)
add_dependencies(clijudgepp clijudgepp_assets)

target_include_directories(clijudgepp PRIVATE
  "${CLIJUDGE_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(clijudgepp PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(WIN32)
  enable_language(RC)
  target_sources(clijudgepp PRIVATE src/clijudgepp.rc)
  target_compile_definitions(clijudgepp PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)
  target_link_libraries(clijudgepp PRIVATE ws2_32 winhttp psapi userenv ole32)
  if(MINGW)
    # 与主项目一致：静态链接运行库，避免缺少 libgcc/libstdc++/libwinpthread DLL
    set_target_properties(clijudgepp PROPERTIES LINK_FLAGS "-static")
  endif()
endif()

install(TARGETS clijudgepp RUNTIME DESTINATION bin)
