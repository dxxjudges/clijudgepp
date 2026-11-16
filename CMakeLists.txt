cmake_minimum_required(VERSION 3.16)
project(CliJudgePP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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

# ── SDL2（本地 .deps/sdl2-src 优先，否则按固定版本下载） ─────────────
# 桌面: Windows 静态链接（单文件 exe）；Linux 动态库（artifact 内附带 .so）。
# Android: 必须动态库（SDLActivity 依次加载 libSDL2.so 与 libmain.so）。
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
# 关闭 ccache：PATH 上的第三方 ccache 包装 NDK clang 时会挂死
set(SDL_CCACHE OFF CACHE BOOL "" FORCE)
if(ANDROID)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
elseif(WIN32)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
else()
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()

set(SDL2_LOCAL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.deps/sdl2-src")
if(EXISTS "${SDL2_LOCAL_SOURCE}/CMakeLists.txt")
  message(STATUS "Using local SDL2 source: ${SDL2_LOCAL_SOURCE}")
  add_subdirectory("${SDL2_LOCAL_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/sdl2" EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  # 不使用 DOWNLOAD_EXTRACT_TIMESTAMP：Android SDK 自带的 CMake 3.22 不支持该参数
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.32.10.tar.gz
    URL_HASH SHA256=03F9D7C191A837525C9CDA6406AF2F2E48BE02B5E7EB03D949CC9F1E9CA41C8B)
  FetchContent_MakeAvailable(SDL2)
endif()

# ── Dear ImGui（静态库 + SDL2/SDLRenderer2 后端） ───────────────────
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
  message(FATAL_ERROR "Dear ImGui not found at ${IMGUI_DIR} (git submodule update --init)")
endif()

add_library(imgui STATIC
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp")
target_include_directories(imgui PUBLIC
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/backends")
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# ── GUI 源码 ─────────────────────────────────────────────────────
set(GUI_SOURCES
  src/main.cpp
  src/markdown.cpp
  src/data_service.cpp
  src/ui_app.cpp
  src/i18n.cpp
  src/i18n_online.cpp
  src/miniz_impl.cpp)

if(ANDROID)
  add_library(main SHARED ${GUI_SOURCES})
  target_include_directories(main PRIVATE
    "${CLIJUDGE_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  # ${CMAKE_DL_LIBS}: lang.h 的 HTTP 客户端运行时 dlopen OpenSSL
  target_link_libraries(main PRIVATE imgui Threads::Threads log android ${CMAKE_DL_LIBS})
  set_target_properties(main PROPERTIES OUTPUT_NAME "main")
else()
  add_executable(clijudgepp ${GUI_SOURCES})
  target_include_directories(clijudgepp PRIVATE
    "${CLIJUDGE_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(clijudgepp PRIVATE imgui SDL2::SDL2 SDL2::SDL2main Threads::Threads ${CMAKE_DL_LIBS})

  if(WIN32)
    enable_language(RC)
    target_sources(clijudgepp PRIVATE src/clijudgepp.rc)
    target_compile_definitions(clijudgepp PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)
    target_link_libraries(clijudgepp PRIVATE ws2_32 winhttp psapi userenv ole32 shell32)
    if(NOT MSVC)
      # 与主项目一致：静态链接运行库，避免缺少 libgcc/libstdc++/libwinpthread DLL
      # （MinGW 与 llvm-mingw 交叉工具链都适用）
      target_link_options(clijudgepp PRIVATE -static)
    endif()
    set_target_properties(clijudgepp PROPERTIES WIN32_EXECUTABLE TRUE)
  elseif(UNIX)
    set_target_properties(clijudgepp PROPERTIES
      INSTALL_RPATH "$ORIGIN" BUILD_RPATH "$ORIGIN")
  endif()

  install(TARGETS clijudgepp RUNTIME DESTINATION bin)
endif()

# ── 单元测试（仅桌面；Android 交叉构建不编测试） ─────────────────────
if(NOT ANDROID)
  option(CLIJUDGEPP_BUILD_TESTS "Build clijudgepp unit tests" ON)
  if(CLIJUDGEPP_BUILD_TESTS)
    enable_testing()
    add_executable(clijudgepp_tests
      tests/test_main.cpp
      src/markdown.cpp
      src/i18n.cpp)
    target_include_directories(clijudgepp_tests PRIVATE
      "${CLIJUDGE_INCLUDE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(clijudgepp_tests PRIVATE imgui Threads::Threads)
    if(WIN32 AND NOT MSVC)
      target_link_options(clijudgepp_tests PRIVATE -static)
    endif()
    add_test(NAME clijudgepp_tests COMMAND clijudgepp_tests)
  endif()
endif()
