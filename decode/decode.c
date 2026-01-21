#include "decode.h"
#include "utils.h"
#include "ceaser_cipher.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <png.h>

#define HEADER_PREFIX "png_"
#define HEADER_PREFIX_LEN 4
#define MAX_HEADER_SIZE 1024

static int verify_decode_arguments(const struct cmd_arguments *arguments) {
    if (!arguments->png_path || arguments->given_rotations == false) {
        fprintf(stderr, "Missing required arguments for decoding\n");
        return -1;
    }
    return 0;
}

/**
 * Decode a single character from the PNG pixels starting at pixel_index
 * Each character uses 4 pixels (2 bits per pixel from green and blue channels)
 */
static unsigned char decode_char_from_pixels(png_byte *row_pointers[], const struct png_handler *png_handler,
                                              unsigned int *pixel_index, const int channels) {
    unsigned char character = 0;
    const unsigned int total_pixels = png_handler->height * png_handler->width;

    for (int bit_idx = 0; bit_idx < 8 && *pixel_index < total_pixels; bit_idx += 2, (*pixel_index)++) {
        const unsigned int row = *pixel_index / png_handler->width;
        const unsigned int col = *pixel_index % png_handler->width;
        const unsigned int pixel_offset = col * channels;

        // Extract bits from green and blue channels (LSB)
        int green_bit = row_pointers[row][pixel_offset + 1] & 1;
        int blue_bit = row_pointers[row][pixel_offset + 2] & 1;

        character |= (green_bit << bit_idx);
        character |= (blue_bit << (bit_idx + 1));
    }

    return character;
}

/**
 * Extract the file extension from a filename
 * Returns pointer to the extension (including the dot) or empty string if none
 */
static const char *get_file_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return "";
    }
    return dot;
}

/**
 * Parse the header to extract the original filename and message size
 * Header format: "png_<filename>_<size>_"
 * The size is always a numeric value, so we find the last underscore before
 * the trailing underscore and verify it's followed by digits.
 */
static int parse_header(const char *header, char *filename_out, long *message_size_out) {
    // Check for "png_" prefix
    if (strncmp(header, HEADER_PREFIX, HEADER_PREFIX_LEN) != 0) {
        fprintf(stderr, "Invalid header format: missing 'png_' prefix\n");
        return -1;
    }

    const char *after_prefix = header + HEADER_PREFIX_LEN;

    // Find the trailing underscore
    const size_t header_len = strlen(after_prefix);
    if (header_len == 0 || after_prefix[header_len - 1] != '_') {
        fprintf(stderr, "Invalid header format: missing trailing underscore\n");
        return -1;
    }

    // Work backwards from the trailing underscore to find the size field
    // The size is between the last two underscores
    const char *trailing_underscore = after_prefix + header_len - 1;
    const char *size_end = trailing_underscore; // Points to trailing '_'

    // Go back to find the underscore before the size
    const char *ptr = size_end - 1;
    while (ptr > after_prefix && *ptr != '_') {
        // Verify this is a digit (size must be numeric)
        if (*ptr < '0' || *ptr > '9') {
            // Not a digit - keep going back
            break;
        }
        ptr--;
    }

    // If we didn't find underscore followed by digits, search more carefully
    // Find the last underscore that is followed only by digits until the trailing underscore
    const char *size_start = nullptr;
    for (const char *p = trailing_underscore - 1; p > after_prefix; p--) {
        if (*p == '_') {
            // Check if everything between this underscore and trailing underscore is digits
            bool all_digits = true;
            for (const char *d = p + 1; d < trailing_underscore; d++) {
                if (*d < '0' || *d > '9') {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits && (p + 1) < trailing_underscore) {
                size_start = p;
                break;
            }
        }
    }

    if (size_start == NULL) {
        fprintf(stderr, "Invalid header format: cannot find size delimiter\n");
        return -1;
    }

    // Extract the size
    char size_str[32];
    const size_t size_len = size_end - size_start - 1;
    if (size_len >= sizeof(size_str) || size_len == 0) {
        fprintf(stderr, "Invalid header format: size field too long or empty\n");
        return -1;
    }
    strncpy(size_str, size_start + 1, size_len);
    size_str[size_len] = '\0';

    errno = 0;
    char *endptr;
    const long size = strtol(size_str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || size <= 0) {
        fprintf(stderr, "Invalid header format: invalid size value '%s'\n", size_str);
        return -1;
    }
    *message_size_out = size;

    // Extract the filename (between prefix and size delimiter)
    const size_t filename_len = size_start - after_prefix;
    if (filename_len >= MAX_FILENAME) {
        fprintf(stderr, "Filename too long in header\n");
        return -1;
    }
    strncpy(filename_out, after_prefix, filename_len);
    filename_out[filename_len] = '\0';

    return 0;
}

/**
 * Decode data from PNG pixels
 */
static char *decode_png_data(png_byte *row_pointers[], const struct png_handler *png_handler,
                              const unsigned int start_pixel_index, const long num_bytes, const long ceaser_rotations) {
    const int channels = png_get_channels(png_handler->png_ptr, png_handler->info_ptr);
    unsigned int pixel_index = start_pixel_index;

    char *data = malloc(num_bytes + 1);
    if (!data) {
        fprintf(stderr, "Failed to allocate memory for decoded data\n");
        return nullptr;
    }

    for (long i = 0; i < num_bytes; i++) {
        data[i] = decode_char_from_pixels(row_pointers, png_handler, &pixel_index, channels);
    }
    data[num_bytes] = '\0';

    // Decrypt the data
    decrypt_message(data, num_bytes, ceaser_rotations);

    return data;
}

/**
 * Read the header from the encoded PNG
 * The header format is: png_<filename>_<size>_
 * Since the filename can contain underscores, we need to find the size field
 * which is digits followed by a trailing underscore.
 */
static int read_and_parse_header(png_byte *row_pointers[], const struct png_handler *png_handler,
                                  long ceaser_rotations, char *filename_out,
                                  long *message_size_out, unsigned int *header_end_pixel) {
    const int channels = png_get_channels(png_handler->png_ptr, png_handler->info_ptr);
    unsigned int pixel_index = 0;

    char header_buffer[MAX_HEADER_SIZE];
    int header_idx = 0;
    bool found_header_end = false;

    // Read characters looking for the pattern: digits followed by underscore
    // which indicates the end of the size field (end of header)
    int consecutive_digits = 0;

    while (header_idx < MAX_HEADER_SIZE - 1 && !found_header_end) {
        unsigned char c = decode_char_from_pixels(row_pointers, png_handler, &pixel_index, channels);

        // Decrypt the character
        c = c - ceaser_rotations;

        header_buffer[header_idx++] = c;

        // Track if we're seeing digits
        if (c >= '0' && c <= '9') {
            consecutive_digits++;
        } else if (c == '_' && consecutive_digits > 0) {
            // We found digits followed by underscore - this should be the end
            // But we need to make sure we've seen at least "png_" first
            if (header_idx > 4) {
                found_header_end = true;
            }
            consecutive_digits = 0;
        } else {
            consecutive_digits = 0;
        }
    }

    if (!found_header_end) {
        fprintf(stderr, "Failed to read complete header\n");
        return -1;
    }

    header_buffer[header_idx] = '\0';
    *header_end_pixel = pixel_index;


    // Parse the header
    return parse_header(header_buffer, filename_out, message_size_out);
}

int handle_decode(const struct cmd_arguments *arguments) {
    int return_value = verify_decode_arguments(arguments);
    if (return_value == -1) {
        usage();
    }


    struct png_handler *png_handler = malloc(sizeof(struct png_handler));
    if (!png_handler) {
        fprintf(stderr, "Failed to allocate memory for png_handler\n");
        return -1;
    }

    // Use a temporary path for loading (we won't modify the file)
    const char *temp_path = "/tmp/decode_temp.png";

    return_value = load_png_file(arguments->png_path, temp_path, png_handler);
    if (return_value == -1) {
        free(png_handler);
        return -1;
    }


    // Get PNG dimensions
    png_handler->height = png_get_image_height(png_handler->png_ptr, png_handler->info_ptr);
    png_handler->width = png_get_image_width(png_handler->png_ptr, png_handler->info_ptr);
    png_handler->bit_depth = png_get_bit_depth(png_handler->png_ptr, png_handler->info_ptr);
    png_handler->color_type = png_get_color_type(png_handler->png_ptr, png_handler->info_ptr);

    // Allocate and read row pointers
    png_byte *row_pointers[png_handler->height];
    for (int row = 0; row < png_handler->height; row++) {
        row_pointers[row] = nullptr;
    }

    for (int row = 0; row < png_handler->height; row++) {
        row_pointers[row] = png_malloc(png_handler->png_ptr, png_get_rowbytes(png_handler->png_ptr, png_handler->info_ptr));
    }

    png_read_image(png_handler->png_ptr, row_pointers);

    // Read and parse the header
    char original_filename[512];
    long message_size;
    unsigned int header_end_pixel;

    return_value = read_and_parse_header(row_pointers, png_handler, arguments->ceaser_rotations,
                                          original_filename,
                                          &message_size, &header_end_pixel);
    if (return_value == -1) {
        fprintf(stderr, "Failed to parse header from encoded PNG\n");
        goto cleanup;
    }

    printf("Decoded header:\n");
    printf("  Original filename: %s\n", original_filename);
    printf("  Message size: %ld bytes\n", message_size);

    // Decode the message data
    char *decoded_data = decode_png_data(row_pointers, png_handler, header_end_pixel,
                                          message_size, arguments->ceaser_rotations);
    if (!decoded_data) {
        return_value = -1;
        goto cleanup;
    }

    // Create output filename: decoded.<extension>
    const char *extension = get_file_extension(original_filename);
    char output_filename[256];
    if (strlen(extension) > 0) {
        snprintf(output_filename, sizeof(output_filename), "decoded%s", extension);
    } else {
        snprintf(output_filename, sizeof(output_filename), "decoded.bin");
    }

    // Write the decoded data to file
    FILE *output_file = fopen(output_filename, "wb");
    if (!output_file) {
        fprintf(stderr, "Failed to create output file: %s\n", strerror(errno));
        free(decoded_data);
        return_value = -1;
        goto cleanup;
    }

    const size_t written = fwrite(decoded_data, 1, message_size, output_file);
    fclose(output_file);
    free(decoded_data);

    if (written != (size_t)message_size) {
        fprintf(stderr, "Failed to write all data to output file\n");
        return_value = -1;
        goto cleanup;
    }

    printf("Successfully decoded message to: %s\n", output_filename);
    return_value = 0;

cleanup:
    for (int row = 0; row < png_handler->height; row++) {
        if (row_pointers[row]) {
            free(row_pointers[row]);
        }
    }

    fclose(png_handler->png_file_ptr);
    png_destroy_read_struct(&png_handler->png_ptr, &png_handler->info_ptr, nullptr);
    free(png_handler);

    // Clean up temp file
    unlink(temp_path);

    return return_value;
}