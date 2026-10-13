include("${CMAKE_CURRENT_LIST_DIR}/ps5-llama-toolchain.cmake")
string(APPEND CMAKE_C_FLAGS_INIT " -DGGML_MAX_NAME=160")
string(APPEND CMAKE_CXX_FLAGS_INIT " -DGGML_MAX_NAME=160 -include${CMAKE_CURRENT_LIST_DIR}/ps5-isascii-compat.h")
