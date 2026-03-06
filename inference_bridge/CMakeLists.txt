# inference-bridge: Aeron <-> Triton In-Process C++ Bridge
# Links against Aeron C++ client and Triton Inference Server shared library (libtritonserver.so)
cmake_minimum_required(VERSION 3.14)
project(inference-bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# -----------------------------------------------------------------------------
# Aeron C++ client
# Set AERON_ROOT to Aeron build/install directory, or use find_package if installed
# -----------------------------------------------------------------------------
if(NOT DEFINED AERON_ROOT)
  set(AERON_ROOT "$ENV{AERON_ROOT}" CACHE PATH "Aeron C++ client root (build or install)")
endif()
if(AERON_ROOT)
  set(AERON_INCLUDE_DIR "${AERON_ROOT}/aeron-client/src/main/cpp")
  set(AERON_AGRONA_DIR "${AERON_ROOT}/agrona/src/main/c")
  # Adjust if your build uses a different layout (e.g. cppbuild/Debug)
  set(AERON_LIB_DIR "${AERON_ROOT}/cppbuild/Debug" CACHE PATH "Aeron library directory")
  if(NOT EXISTS "${AERON_LIB_DIR}")
    set(AERON_LIB_DIR "${AERON_ROOT}/cppbuild/Release")
  endif()
  include_directories(${AERON_INCLUDE_DIR} ${AERON_AGRONA_DIR})
  link_directories(${AERON_LIB_DIR})
  link_libraries(aeron)
endif()

# -----------------------------------------------------------------------------
# Triton Inference Server (In-Process C API)
# Set TRITON_ROOT to Triton install (e.g. /opt/tritonserver) or container path
# -----------------------------------------------------------------------------
if(NOT DEFINED TRITON_ROOT)
  set(TRITON_ROOT "$ENV{TRITON_ROOT}" CACHE PATH "Triton Inference Server root")
endif()
if(TRITON_ROOT)
  set(TRITON_INCLUDE_DIR "${TRITON_ROOT}/include")
  set(TRITON_LIB_DIR "${TRITON_ROOT}/lib")
  if(EXISTS "${TRITON_ROOT}/lib/x86_64-linux-gnu")
    set(TRITON_LIB_DIR "${TRITON_ROOT}/lib/x86_64-linux-gnu")
  endif()
  include_directories(${TRITON_INCLUDE_DIR})
  link_directories(${TRITON_LIB_DIR})
  link_libraries(tritonserver)
endif()

# -----------------------------------------------------------------------------
# Application
# -----------------------------------------------------------------------------
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SOURCES
  src/main.cpp
  src/AeronSubscriber.cpp
  src/AeronPublisher.cpp
  src/TritonBridge.cpp
)
add_executable(inference-bridge ${SOURCES})
target_include_directories(inference-bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(AERON_ROOT)
  target_compile_definitions(inference-bridge PRIVATE USE_AERON)
endif()
if(TRITON_ROOT)
  target_compile_definitions(inference-bridge PRIVATE USE_TRITON)
endif()

# Optional: install
install(TARGETS inference-bridge RUNTIME DESTINATION bin)
