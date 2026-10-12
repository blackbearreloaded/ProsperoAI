include("${CMAKE_CURRENT_LIST_DIR}/ps5-llama-toolchain.cmake")
string(APPEND CMAKE_CXX_FLAGS_INIT " -include${CMAKE_CURRENT_LIST_DIR}/ps5-isascii-compat.h")
