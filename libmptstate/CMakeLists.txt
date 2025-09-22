# libmptstate/CMakeLists.txt
file(GLOB SRC_LIST "*.cpp" "*.hpp")
file(GLOB HEADERS  "*.h")

# 保险：清掉可能的旧缓存变量
unset(POINTPROOFS CACHE)
unset(POINTPROOFS_LIBRARIES CACHE)

# 1) 指定 .a 的绝对路径并校验
set(POINTPROOFS_A "${CMAKE_CURRENT_LIST_DIR}/libpointproofs.a")
if(NOT EXISTS "${POINTPROOFS_A}")
  message(FATAL_ERROR "pointproofs.a not found at: ${POINTPROOFS_A}")
endif()

# 2) 导入静态库，给所有配置都设置位置
add_library(pointproofs STATIC IMPORTED GLOBAL)
set_target_properties(pointproofs PROPERTIES
  IMPORTED_LOCATION               "${POINTPROOFS_A}"
  IMPORTED_LOCATION_DEBUG         "${POINTPROOFS_A}"
  IMPORTED_LOCATION_RELEASE       "${POINTPROOFS_A}"
  IMPORTED_LOCATION_RELWITHDEBINFO "${POINTPROOFS_A}"
  IMPORTED_LOCATION_MINSIZEREL    "${POINTPROOFS_A}"
  INTERFACE_INCLUDE_DIRECTORIES   "${CMAKE_CURRENT_LIST_DIR}"   # 这里放着 pointproofs.h
)

# 3) mptstate 链接它（PUBLIC 让头文件路径传给下游）
add_library(mptstate ${SRC_LIST} ${HEADERS})
target_link_libraries(mptstate
  PUBLIC
    devcore ethereum devcrypto ethcore evm
    Boost::boost TBB::tbb RocksDB::rocksdb
    pointproofs erasure
)

# 4) 测试只连 mptstate
add_executable(mptstate-test main.cpp)
target_link_libraries(mptstate-test PRIVATE mptstate)
