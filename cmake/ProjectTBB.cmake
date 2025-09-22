include(ExternalProject)

set(prefix "${CMAKE_BINARY_DIR}/deps")

# 统一约束：强制装进 lib（不是 lib64）
set(_tbb_install_libdir "lib")

# 根据“共享/静态”二选一（默认共享库更省心）
set(_tbb_build_shared ON)   # 如果要静态库，设为 OFF
set(_tbb_build_static OFF)  # 如果要静态库，设为 ON

# 预先写入 CACHE —— 这样后续任何 find_library 或问题宏都不会把它改成 -NOTFOUND
# 共享库：.so；静态库：.a
if(_tbb_build_shared)
  set(TBB_LIBRARY "${prefix}/${_tbb_install_libdir}/libtbb.so" CACHE FILEPATH "" FORCE)
else()
  set(TBB_LIBRARY "${prefix}/${_tbb_install_libdir}/libtbb.a" CACHE FILEPATH "" FORCE)
endif()
set(TBB_LIBRARIES   "${TBB_LIBRARY}"          CACHE STRING  "" FORCE)
set(TBB_INCLUDE_DIR "${prefix}/include"       CACHE PATH    "" FORCE)
set(TBB_FOUND       TRUE                      CACHE BOOL    "" FORCE)
# 有些宏还会读 DEBUG 变量，顺手喂一下
set(TBB_LIBRARY_DEBUG "${TBB_LIBRARY}"        CACHE FILEPATH "" FORCE)

ExternalProject_Add(
  tbb
  PREFIX "${prefix}"
  DOWNLOAD_NAME oneTBB-2021.12.0.tar.gz
  DOWNLOAD_NO_PROGRESS 1
  URL https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.12.0.tar.gz
  # URL_HASH SHA256=bb46e4c2d41541a72424d4168f41d81f93657fd259772bba9c8f03b0bcadfe27

  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_LIBDIR=${_tbb_install_libdir}  # ← 关键：避免装到 lib64
    -DCMAKE_BUILD_TYPE=Release
    -DTBB_TEST=OFF
    -DTBB_EXAMPLES=OFF
    -DTBB_STRICT=OFF
    -DTBB_BUILD_SHARED=${_tbb_build_shared}
    -DTBB_BUILD_STATIC=${_tbb_build_static}

  LOG_CONFIGURE 1
  LOG_BUILD     1
  LOG_INSTALL   1
)

# IMPORTED 目标（只用目标来链接，不直接用路径变量）
if(_tbb_build_shared)
  add_library(TBB SHARED IMPORTED)
else()
  add_library(TBB STATIC IMPORTED)
endif()
set_property(TARGET TBB PROPERTY IMPORTED_LOCATION "${TBB_LIBRARY}")
set_property(TARGET TBB PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${TBB_INCLUDE_DIR}")
add_dependencies(TBB tbb)
