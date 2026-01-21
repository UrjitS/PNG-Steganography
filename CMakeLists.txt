cmake_minimum_required(VERSION 3.5)
project(png_steganography C)

set(CMAKE_C_STANDARD 23)

# Collect sources from subdirectories
file(GLOB_RECURSE DECODE_SRCS "${CMAKE_SOURCE_DIR}/decode/*.c")
file(GLOB_RECURSE ENCODE_SRCS "${CMAKE_SOURCE_DIR}/encode/*.c")
file(GLOB_RECURSE UTILS_SRCS  "${CMAKE_SOURCE_DIR}/utils/*.c")
file(GLOB_RECURSE ENCRYPTION_SRCS  "${CMAKE_SOURCE_DIR}/encryption/*.c")

set(SRC_FILES
    main.c
    ${DECODE_SRCS}
    ${ENCODE_SRCS}
    ${UTILS_SRCS}
    ${ENCRYPTION_SRCS}
)

add_executable(png_steganography ${SRC_FILES})

target_include_directories(png_steganography PRIVATE
    ${CMAKE_SOURCE_DIR}/decode
    ${CMAKE_SOURCE_DIR}/encode
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/encryption
)
find_package(PNG REQUIRED)

# after add_executable(...)
target_link_libraries(png_steganography PRIVATE PNG::PNG)

# Test executable
file(GLOB TEST_SRCS "${CMAKE_SOURCE_DIR}/tests/*.c")

add_executable(test_encode
    tests/test_encode.c
    ${DECODE_SRCS}
    ${ENCODE_SRCS}
    ${UTILS_SRCS}
    ${ENCRYPTION_SRCS}
)

target_include_directories(test_encode PRIVATE
    ${CMAKE_SOURCE_DIR}/decode
    ${CMAKE_SOURCE_DIR}/encode
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/encryption
)

target_link_libraries(test_encode PRIVATE PNG::PNG)

# Enable testing
enable_testing()
add_test(NAME encoding_tests COMMAND test_encode)
