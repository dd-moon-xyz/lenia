if(CMAKE_CXX_COMPILER OR CMAKE_CUDA_HOST_COMPILER OR DEFINED ENV{CXX} OR DEFINED ENV{CUDAHOSTCXX})
    return()
endif()

if(NOT CMAKE_CUDA_COMPILER)
    if(DEFINED ENV{CUDACXX})
        set(CMAKE_CUDA_COMPILER "$ENV{CUDACXX}")
    else()
        find_program(CMAKE_CUDA_COMPILER NAMES nvcc REQUIRED)
    endif()
endif()

find_program(LENIA_DEFAULT_CXX NAMES g++ REQUIRED)
set(LENIA_HOST_CANDIDATES "${LENIA_DEFAULT_CXX}")
cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST LENIA_SEARCH_PATHS)

foreach(LENIA_SEARCH_PATH IN LISTS LENIA_SEARCH_PATHS)
    file(GLOB LENIA_VERSIONED_CXX "${LENIA_SEARCH_PATH}/g++-*")
    list(SORT LENIA_VERSIONED_CXX COMPARE NATURAL ORDER DESCENDING)
    list(APPEND LENIA_HOST_CANDIDATES ${LENIA_VERSIONED_CXX})
endforeach()

list(REMOVE_DUPLICATES LENIA_HOST_CANDIDATES)
set(LENIA_PROBE_DIRECTORY "${CMAKE_BINARY_DIR}/CMakeFiles/lenia-cuda-host")
file(MAKE_DIRECTORY "${LENIA_PROBE_DIRECTORY}")
file(WRITE "${LENIA_PROBE_DIRECTORY}/probe.cu"
    "#include <vector>\n#include <concepts>\n__global__ void probe() {}\nint main() { std::vector<int> values{1}; return values.front(); }\n"
)

foreach(LENIA_HOST_CANDIDATE IN LISTS LENIA_HOST_CANDIDATES)
    execute_process(
        COMMAND "${CMAKE_CUDA_COMPILER}" -std=c++20
            -ccbin "${LENIA_HOST_CANDIDATE}"
            -c "${LENIA_PROBE_DIRECTORY}/probe.cu"
            -o "${LENIA_PROBE_DIRECTORY}/probe.o"
        RESULT_VARIABLE LENIA_PROBE_RESULT
        OUTPUT_VARIABLE LENIA_PROBE_OUTPUT
        ERROR_VARIABLE LENIA_PROBE_ERROR
        TIMEOUT 30
    )

    file(APPEND "${LENIA_PROBE_DIRECTORY}/results.log"
        "${LENIA_HOST_CANDIDATE}: ${LENIA_PROBE_RESULT}\n${LENIA_PROBE_OUTPUT}${LENIA_PROBE_ERROR}\n"
    )

    if(LENIA_PROBE_RESULT STREQUAL "0")
        set(CMAKE_CXX_COMPILER "${LENIA_HOST_CANDIDATE}" CACHE FILEPATH "C++ compiler")
        set(CMAKE_CUDA_HOST_COMPILER "${LENIA_HOST_CANDIDATE}" CACHE FILEPATH "CUDA host compiler")

        if(NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
            cmake_path(GET LENIA_HOST_CANDIDATE PARENT_PATH LENIA_HOST_DIRECTORY)
            cmake_path(GET LENIA_HOST_CANDIDATE FILENAME LENIA_HOST_NAME)
            string(REPLACE "g++" "gcc" LENIA_C_NAME "${LENIA_HOST_NAME}")
            find_program(LENIA_C_COMPILER NAMES "${LENIA_C_NAME}" PATHS "${LENIA_HOST_DIRECTORY}" NO_DEFAULT_PATH)

            if(LENIA_C_COMPILER)
                set(CMAKE_C_COMPILER "${LENIA_C_COMPILER}" CACHE FILEPATH "C compiler")
            endif()
        endif()

        message(STATUS "Selected CUDA-compatible C++ compiler: ${LENIA_HOST_CANDIDATE}")
        return()
    endif()

    message(STATUS "CUDA C++20 check failed for ${LENIA_HOST_CANDIDATE}")
endforeach()

message(FATAL_ERROR
    "No installed g++ compiler passed the CUDA C++20 check. Install a compiler compatible "
    "with your CUDA Toolkit, or upgrade the Toolkit. Probe output: ${LENIA_PROBE_DIRECTORY}/results.log"
)
