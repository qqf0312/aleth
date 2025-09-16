file(GLOB sources "*.cpp" "*.h")

file(GLOB_RECURSE ec_sources
  "${CMAKE_CURRENT_LIST_DIR}/ec/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/ec/*.h"
)
list(APPEND sources ${ec_sources})

add_library(p2p ${sources})

# 让外部可以：#include <libp2p/ec/ErasureCapability.h>
target_include_directories(p2p PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)

target_link_libraries(p2p PUBLIC devcrypto devcore)
target_include_directories(p2p SYSTEM PRIVATE ${CRYPTOPP_INCLUDE_DIR})

if(MINIUPNPC)
  target_link_libraries(p2p PRIVATE "${MINIUPNPC_LIBRARY}")
  target_include_directories(p2p SYSTEM PRIVATE "${MINIUPNPC_INCLUDE_DIR}")
  target_compile_definitions(p2p PRIVATE ETH_MINIUPNPC)
endif()