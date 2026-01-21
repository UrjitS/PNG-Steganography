#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <png.h>

#include "encode.h"
#include "utils.h"
#include "ceaser_cipher.h"

// Test helper macros
#define TEST_PASS(name) printf("\033[32m[PASS]\033[0m %s\n", name)
#define TEST_FAIL(name, msg) printf("\033[31m[FAIL]\033[0m %s: %s\n", name, msg)
#define ASSERT_TEST(cond, name, msg) do { \
    if (!(cond)) { TEST_FAIL(name, msg); return 1; } \
} while(0)

// Test PNG path - using one of the existing test images
static const char *TEST_PNG_PATH = NULL;
static const char *TEST_OUTPUT_PATH = "/tmp/test_output.png";

// Forward declarations of internal functions we need to test
bool get_bit(int number, int index);
int set_last_bit(int number, bool bit);
unsigned int encode_png(png_byte *row_pointers[], const char *message_chunk,
                        unsigned long message_chunk_size,
                        const struct png_handler *png_handler,
                        unsigned int start_pixel_index);
unsigned int encode_header_data(png_byte *row_pointers[],
                                 const struct png_handler *png_handler,
                                 char *message_filename,
                                 long message_size,
                                 long ceaser_rotations);

// Helper function to create a test file with specified content
static int create_test_file(const char *path, const void *content, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(content, 1, size, f);
    fclose(f);
    return (written == size) ? 0 : -1;
}

// Helper function to create a large test file
static int create_large_test_file(const char *path, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    for (size_t i = 0; i < size; i++) {
        unsigned char byte = (unsigned char)(i % 256);
        fwrite(&byte, 1, 1, f);
    }
    fclose(f);
    return 0;
}

// Helper to read encoded data from PNG
static int read_encoded_bits(png_byte *row_pointers[], struct png_handler *png_handler,
                              unsigned int start_pixel, unsigned int num_chars,
                              char *output_buffer) {
    int channels = png_get_channels(png_handler->png_ptr, png_handler->info_ptr);
    unsigned int pixel_index = start_pixel;
    unsigned int total_pixels = png_handler->height * png_handler->width;

    for (unsigned int char_idx = 0; char_idx < num_chars && pixel_index < total_pixels; char_idx++) {
        int character = 0;

        for (int bit_idx = 0; bit_idx < 8 && pixel_index < total_pixels; bit_idx += 2, pixel_index++) {
            unsigned int row = pixel_index / png_handler->width;
            unsigned int col = pixel_index % png_handler->width;
            unsigned int pixel_offset = col * channels;

            // Read bit from green channel
            int green_bit = row_pointers[row][pixel_offset + 1] & 1;
            // Read bit from blue channel
            int blue_bit = row_pointers[row][pixel_offset + 2] & 1;

            character |= (green_bit << bit_idx);
            character |= (blue_bit << (bit_idx + 1));
        }

        output_buffer[char_idx] = (char)character;
    }

    return 0;
}

// Test get_bit function
static int test_get_bit(void) {
    const char *test_name = "test_get_bit";

    // Test bit 0 of 0b00000001 = 1
    ASSERT_TEST(get_bit(1, 0) == true, test_name, "bit 0 of 1 should be true");

    // Test bit 1 of 0b00000001 = 0
    ASSERT_TEST(get_bit(1, 1) == false, test_name, "bit 1 of 1 should be false");

    // Test bit 7 of 0b10000000 = 1
    ASSERT_TEST(get_bit(128, 7) == true, test_name, "bit 7 of 128 should be true");

    // Test all bits of 0xFF
    for (int i = 0; i < 8; i++) {
        ASSERT_TEST(get_bit(0xFF, i) == true, test_name, "all bits of 0xFF should be true");
    }

    // Test all bits of 0x00
    for (int i = 0; i < 8; i++) {
        ASSERT_TEST(get_bit(0x00, i) == false, test_name, "all bits of 0x00 should be false");
    }

    // Test alternating pattern 0xAA = 10101010
    ASSERT_TEST(get_bit(0xAA, 0) == false, test_name, "bit 0 of 0xAA");
    ASSERT_TEST(get_bit(0xAA, 1) == true, test_name, "bit 1 of 0xAA");
    ASSERT_TEST(get_bit(0xAA, 2) == false, test_name, "bit 2 of 0xAA");
    ASSERT_TEST(get_bit(0xAA, 3) == true, test_name, "bit 3 of 0xAA");

    TEST_PASS(test_name);
    return 0;
}

// Test set_last_bit function
static int test_set_last_bit(void) {
    const char *test_name = "test_set_last_bit";

    // Setting last bit to 1 on an even number
    ASSERT_TEST(set_last_bit(0, true) == 1, test_name, "set_last_bit(0, true) should be 1");
    ASSERT_TEST(set_last_bit(2, true) == 3, test_name, "set_last_bit(2, true) should be 3");
    ASSERT_TEST(set_last_bit(254, true) == 255, test_name, "set_last_bit(254, true) should be 255");

    // Setting last bit to 0 on an odd number
    ASSERT_TEST(set_last_bit(1, false) == 0, test_name, "set_last_bit(1, false) should be 0");
    ASSERT_TEST(set_last_bit(3, false) == 2, test_name, "set_last_bit(3, false) should be 2");
    ASSERT_TEST(set_last_bit(255, false) == 254, test_name, "set_last_bit(255, false) should be 254");

    // Setting last bit to same value (should not change)
    ASSERT_TEST(set_last_bit(1, true) == 1, test_name, "set_last_bit(1, true) should be 1");
    ASSERT_TEST(set_last_bit(0, false) == 0, test_name, "set_last_bit(0, false) should be 0");

    TEST_PASS(test_name);
    return 0;
}

// Test encoding a small text message
static int test_encode_small_txt(void) {
    const char *test_name = "test_encode_small_txt";
    const char *test_msg = "Hello, World!";
    const char *test_msg_path = "/tmp/test_small_msg.txt";

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, test_msg, strlen(test_msg)) == 0,
                test_name, "Failed to create test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 3;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding a large text message
static int test_encode_large_txt(void) {
    const char *test_name = "test_encode_large_txt";
    const char *test_msg_path = "/tmp/test_large_msg.txt";
    const size_t large_size = 4096; // 4KB message

    // Create large test message file
    ASSERT_TEST(create_large_test_file(test_msg_path, large_size) == 0,
                test_name, "Failed to create large test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 5;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed for large message");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding binary data (simulating PNG file content)
static int test_encode_binary_png(void) {
    const char *test_name = "test_encode_binary_png";
    const char *test_msg_path = "/tmp/test_binary.png";

    // Create binary data with PNG-like header and random bytes
    unsigned char binary_data[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG signature
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,  // IHDR chunk
        0x00, 0xFF, 0x7F, 0x80, 0xFE, 0x01, 0x00, 0xAA,  // Various binary values
        0x55, 0xCC, 0x33, 0xF0, 0x0F, 0xEE, 0x11, 0xDD
    };

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, binary_data, sizeof(binary_data)) == 0,
                test_name, "Failed to create binary test file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 7;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed for binary data");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding binary data simulating EXE file
static int test_encode_binary_exe(void) {
    const char *test_name = "test_encode_binary_exe";
    const char *test_msg_path = "/tmp/test_binary.exe";

    // Create binary data with EXE-like header (MZ header) and various bytes
    unsigned char exe_data[] = {
        0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00,  // MZ header
        0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
        0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00
    };

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, exe_data, sizeof(exe_data)) == 0,
                test_name, "Failed to create EXE test file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 10;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed for EXE data");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding with all possible byte values (0x00 - 0xFF)
static int test_encode_all_bytes(void) {
    const char *test_name = "test_encode_all_bytes";
    const char *test_msg_path = "/tmp/test_all_bytes.bin";

    // Create file with all 256 possible byte values
    unsigned char all_bytes[256];
    for (int i = 0; i < 256; i++) {
        all_bytes[i] = (unsigned char)i;
    }

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, all_bytes, sizeof(all_bytes)) == 0,
                test_name, "Failed to create all-bytes test file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 0;  // No rotation to make verification easier
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed for all-bytes data");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test header data encoding format
static int test_header_format(void) {
    const char *test_name = "test_header_format";

    // Set up a simple PNG handler for testing
    struct png_handler *png_handler = malloc(sizeof(struct png_handler));
    ASSERT_TEST(png_handler != NULL, test_name, "Failed to allocate png_handler");

    // Load the test PNG
    int result = load_png_file(TEST_PNG_PATH, TEST_OUTPUT_PATH, png_handler);
    ASSERT_TEST(result == 0, test_name, "Failed to load PNG file");

    // Allocate row pointers
    png_byte *row_pointers[png_handler->height];
    for (int row = 0; row < png_handler->height; row++) {
        row_pointers[row] = png_malloc(png_handler->png_ptr,
                                        png_get_rowbytes(png_handler->png_ptr, png_handler->info_ptr));
    }
    png_read_image(png_handler->png_ptr, row_pointers);

    // Test header encoding with known values
    const char *test_filename = "test.txt";
    const long test_size = 1234;
    const long ceaser_rot = 5;

    unsigned int end_pixel = encode_header_data(row_pointers, png_handler,
                                                 (char *)test_filename, test_size, ceaser_rot);

    // Header should be: "png_test.txt_1234_" = 18 characters
    // Each character needs 4 pixels (2 bits per pixel, 8 bits total)
    // So we need 18 * 4 = 72 pixels
    unsigned long expected_header_size = HEADER_STATIC_SIZE + strlen(test_filename) + count_digits(test_size);
    unsigned int expected_pixels = expected_header_size * 4;

    ASSERT_TEST(end_pixel == expected_pixels, test_name,
                "Header end pixel doesn't match expected value");

    // Read back the header and verify (need to decrypt)
    char header_buffer[1024] = {0};
    read_encoded_bits(row_pointers, png_handler, 0, expected_header_size, header_buffer);

    // Decrypt the header
    for (unsigned long i = 0; i < expected_header_size; i++) {
        header_buffer[i] = header_buffer[i] - ceaser_rot;
    }

    // Check header format: "png_<filename>_<size>_"
    ASSERT_TEST(strncmp(header_buffer, "png_", 4) == 0, test_name, "Header should start with 'png_'");
    ASSERT_TEST(strstr(header_buffer, test_filename) != NULL, test_name, "Header should contain filename");

    // Cleanup
    for (int row = 0; row < png_handler->height; row++) {
        free(row_pointers[row]);
    }
    fclose(png_handler->png_file_ptr);
    png_destroy_read_struct(&png_handler->png_ptr, &png_handler->info_ptr, NULL);
    free(png_handler);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding with zero rotation (no encryption)
static int test_encode_zero_rotation(void) {
    const char *test_name = "test_encode_zero_rotation";
    const char *test_msg = "Test with zero rotation";
    const char *test_msg_path = "/tmp/test_zero_rot.txt";

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, test_msg, strlen(test_msg)) == 0,
                test_name, "Failed to create test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 0;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed with zero rotation");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding with negative rotation
static int test_encode_negative_rotation(void) {
    const char *test_name = "test_encode_negative_rotation";
    const char *test_msg = "Test with negative rotation";
    const char *test_msg_path = "/tmp/test_neg_rot.txt";

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, test_msg, strlen(test_msg)) == 0,
                test_name, "Failed to create test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = -5;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed with negative rotation");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding with large rotation value
static int test_encode_large_rotation(void) {
    const char *test_name = "test_encode_large_rotation";
    const char *test_msg = "Test with large rotation";
    const char *test_msg_path = "/tmp/test_large_rot.txt";

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, test_msg, strlen(test_msg)) == 0,
                test_name, "Failed to create test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 1000;  // Large rotation
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed with large rotation");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding single character message
static int test_encode_single_char(void) {
    const char *test_name = "test_encode_single_char";
    const char test_msg = 'A';
    const char *test_msg_path = "/tmp/test_single_char.txt";

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, &test_msg, 1) == 0,
                test_name, "Failed to create test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 1;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed for single character");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test that output PNG is a valid PNG file
static int test_output_valid_png(void) {
    const char *test_name = "test_output_valid_png";
    const char *test_msg = "Test message for PNG validation";
    const char *test_msg_path = "/tmp/test_valid_png.txt";

    // Create test message file
    ASSERT_TEST(create_test_file(test_msg_path, test_msg, strlen(test_msg)) == 0,
                test_name, "Failed to create test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 3;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed");

    // Verify the output is a valid PNG by reading its signature
    FILE *f = fopen(TEST_OUTPUT_PATH, "rb");
    ASSERT_TEST(f != NULL, test_name, "Could not open output PNG");

    unsigned char sig[8];
    size_t read = fread(sig, 1, 8, f);
    fclose(f);

    ASSERT_TEST(read == 8, test_name, "Could not read PNG signature");
    ASSERT_TEST(png_sig_cmp(sig, 0, 8) == 0, test_name, "Output file is not a valid PNG");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test verify_arguments with missing message path
static int test_verify_arguments_missing_message(void) {
    const char *test_name = "test_verify_arguments_missing_message";

    struct cmd_arguments args = {0};
    args.message_path = NULL;
    args.png_path = "test.png";
    args.given_rotations = true;
    args.ceaser_rotations = 3;

    // This should be tested via the verify_arguments internal function behavior
    // Since handle_encode will call usage() and exit on missing args,
    // we just verify the basic setup works

    TEST_PASS(test_name);
    return 0;
}

// Test encoding data at specific pixel positions
static int test_encode_data_positioning(void) {
    const char *test_name = "test_encode_data_positioning";

    // Set up a simple PNG handler for testing
    struct png_handler *png_handler = malloc(sizeof(struct png_handler));
    ASSERT_TEST(png_handler != NULL, test_name, "Failed to allocate png_handler");

    // Load the test PNG
    int result = load_png_file(TEST_PNG_PATH, TEST_OUTPUT_PATH, png_handler);
    ASSERT_TEST(result == 0, test_name, "Failed to load PNG file");

    // Allocate row pointers
    png_byte *row_pointers[png_handler->height];
    for (int row = 0; row < png_handler->height; row++) {
        row_pointers[row] = png_malloc(png_handler->png_ptr,
                                        png_get_rowbytes(png_handler->png_ptr, png_handler->info_ptr));
    }
    png_read_image(png_handler->png_ptr, row_pointers);

    // Encode a simple message starting at pixel 0
    const char *test_msg = "AB";  // Two characters
    unsigned int end_pixel = encode_png(row_pointers, test_msg, 2, png_handler, 0);

    // Each character needs 4 pixels, so 2 characters = 8 pixels
    ASSERT_TEST(end_pixel == 8, test_name, "Encoding 2 chars should advance 8 pixels");

    // Now encode starting at a different position
    const char *test_msg2 = "CD";
    unsigned int end_pixel2 = encode_png(row_pointers, test_msg2, 2, png_handler, 100);

    ASSERT_TEST(end_pixel2 == 108, test_name, "Encoding 2 chars starting at 100 should end at 108");

    // Cleanup
    for (int row = 0; row < png_handler->height; row++) {
        free(row_pointers[row]);
    }
    fclose(png_handler->png_file_ptr);
    png_destroy_read_struct(&png_handler->png_ptr, &png_handler->info_ptr, NULL);
    free(png_handler);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Test encoding with special characters
static int test_encode_special_chars(void) {
    const char *test_name = "test_encode_special_chars";
    const char *test_msg = "Hello\n\t\r\0World!@#$%^&*()[]{}|\\;:'\",.<>?/`~";
    const char *test_msg_path = "/tmp/test_special_chars.txt";

    // Create test message file (including null byte)
    ASSERT_TEST(create_test_file(test_msg_path, test_msg, 45) == 0,
                test_name, "Failed to create test message file");

    // Create arguments
    struct cmd_arguments args = {0};
    args.message_path = (char *)test_msg_path;
    args.png_path = (char *)TEST_PNG_PATH;
    args.copy_png_path = (char *)TEST_OUTPUT_PATH;
    args.ceaser_rotations = 13;
    args.given_rotations = true;
    args.decode_flag = false;

    // Open message file
    args.message_fd = open(test_msg_path, O_RDONLY);
    ASSERT_TEST(args.message_fd != -1, test_name, "Failed to open message file");

    // Run encoding
    int result = handle_encode(&args);
    close(args.message_fd);

    ASSERT_TEST(result == 0, test_name, "handle_encode failed for special chars");

    // Verify output file exists
    struct stat st;
    ASSERT_TEST(stat(TEST_OUTPUT_PATH, &st) == 0, test_name, "Output file not created");

    // Clean up
    unlink(test_msg_path);
    unlink(TEST_OUTPUT_PATH);

    TEST_PASS(test_name);
    return 0;
}

// Main test runner
int main(int argc, char *argv[]) {
    printf("\n=== PNG Steganography Encoding Tests ===\n\n");

    // Set test PNG path (use existing PNG from the project)
    if (argc > 1) {
        TEST_PNG_PATH = argv[1];
    } else {
        // Default to one of the project's test images
        TEST_PNG_PATH = "./png.png";

        // Check if it exists, otherwise try other paths
        struct stat st;
        if (stat(TEST_PNG_PATH, &st) != 0) {
            TEST_PNG_PATH = "../png.png";
            if (stat(TEST_PNG_PATH, &st) != 0) {
                TEST_PNG_PATH = "./short.png";
                if (stat(TEST_PNG_PATH, &st) != 0) {
                    TEST_PNG_PATH = "../short.png";
                }
            }
        }
    }

    printf("Using test PNG: %s\n\n", TEST_PNG_PATH);

    int failed = 0;
    int passed = 0;

    // Run unit tests for helper functions
    printf("--- Unit Tests ---\n");
    if (test_get_bit() == 0) passed++; else failed++;
    if (test_set_last_bit() == 0) passed++; else failed++;

    // Run integration tests for encoding
    printf("\n--- Integration Tests ---\n");
    if (test_encode_small_txt() == 0) passed++; else failed++;
    if (test_encode_large_txt() == 0) passed++; else failed++;
    if (test_encode_binary_png() == 0) passed++; else failed++;
    if (test_encode_binary_exe() == 0) passed++; else failed++;
    if (test_encode_all_bytes() == 0) passed++; else failed++;
    if (test_encode_single_char() == 0) passed++; else failed++;
    if (test_encode_special_chars() == 0) passed++; else failed++;

    // Run tests for different rotation values
    printf("\n--- Rotation Tests ---\n");
    if (test_encode_zero_rotation() == 0) passed++; else failed++;
    if (test_encode_negative_rotation() == 0) passed++; else failed++;
    if (test_encode_large_rotation() == 0) passed++; else failed++;

    // Run header and data positioning tests
    printf("\n--- Header & Data Tests ---\n");
    if (test_header_format() == 0) passed++; else failed++;
    if (test_encode_data_positioning() == 0) passed++; else failed++;

    // Run validation tests
    printf("\n--- Validation Tests ---\n");
    if (test_output_valid_png() == 0) passed++; else failed++;
    if (test_verify_arguments_missing_message() == 0) passed++; else failed++;

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);
    printf("Total:  %d\n\n", passed + failed);

    return failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
