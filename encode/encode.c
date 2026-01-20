#include <stdlib.h>
#include "encode.h"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <png.h>

#include "ceaser_cipher.h"
#include "utils.h"

int verify_arguments(const struct cmd_arguments * arguments) {
    if (!arguments->message_path || !arguments->png_path || arguments->given_rotations == false) {
        fprintf(stderr, "Missing required arguments for encoding\n");
        return -1;
    }
    return 0;
}

bool get_bit(const int number, const int index) {
    return (number & (1 << index)) != 0;
}

int set_bit(const int number, const int index) {
    return number | (1 << index);
}

int set_last_bit(const int number, const bool bit) {
    if (bit) {
        return number | 1;
    }
    return number & ~1;
}

void encode_png(const char * output_filename, const char * message_chunk, const unsigned long message_chunk_size, struct png_handler * png_handler) {
    png_byte * row_pointers[png_handler->height];

    for (int row = 0; row < png_handler->height; row++)
        row_pointers[row] = nullptr;

    for (int row = 0; row < png_handler->height; row++) {
        row_pointers[row] = png_malloc(png_handler->png_ptr, png_get_rowbytes(png_handler->png_ptr, png_handler->info_ptr));
    }

    png_read_image(png_handler->png_ptr, row_pointers);

    const int channels = png_get_channels(png_handler->png_ptr, png_handler->info_ptr);

    for (int row = 0; row < png_handler->height; row++) {
        for (int col = 0; col < png_handler->width; col++) {
            const int pixel_offset = col * channels;
            // RGB or RGBA
            // printf("R: %hhu, G: %hhu, B: %hhu\n",
            //        row_pointers[row][pixel_offset],     // R
            //        row_pointers[row][pixel_offset + 1], // G
            //        row_pointers[row][pixel_offset + 2]  // B
            // );
            // row_pointers[row][pixel_offset] = set_bit(row_pointers[row][pixel_offset], 0);
            // row_pointers[row][pixel_offset + 1] = set_bit(row_pointers[row][pixel_offset + 1], 0);
            // row_pointers[row][pixel_offset + 2] = set_bit(row_pointers[row][pixel_offset + 2], 0);

            // printf("R: %hhu, G: %hhu, B: %hhu\n",
            //        row_pointers[row][pixel_offset],     // R
            //        row_pointers[row][pixel_offset + 1], // G
            //        row_pointers[row][pixel_offset + 2]  // B
            // );

            for (int index = 0; index < message_chunk_size; index++) {
                const int character_ord = (unsigned char) message_chunk[index];

                for (int current_bit_index = 0; current_bit_index < 8; current_bit_index += 2) {
                    row_pointers[row][pixel_offset + 1] = set_last_bit(row_pointers[row][pixel_offset + 1], get_bit(character_ord, current_bit_index));     // G
                    row_pointers[row][pixel_offset + 2] = set_last_bit(row_pointers[row][pixel_offset + 2], get_bit(character_ord, current_bit_index + 1)); // B
                }
                // printf("Character: %c\n", message_chunk[index]);
            }
        }
    }

    fclose(png_handler->png_file_ptr);
    png_destroy_read_struct(&png_handler->png_ptr, &png_handler->info_ptr, nullptr);

    save_png(output_filename, row_pointers, png_handler);

    for (int row = 0; row < png_handler->height; row++) {
        free(row_pointers[row]);
    }
}

void encode_data(struct png_handler * png_handler, const struct cmd_arguments * arguments, const long message_size) {
    char * chunk;
    long total_bytes_read = 0;
    long last_bytes_read = 0;

    errno = 0;

    while ((chunk = encrypt_message(arguments->message_fd, 512, arguments->ceaser_rotations, &total_bytes_read)) != NULL) {
        encode_png(arguments->copy_png_path, chunk, (total_bytes_read - last_bytes_read), png_handler);
        last_bytes_read = total_bytes_read;
        free(chunk);
    }

    if (errno != 0) {
        // panic
        fprintf(stderr, "An error occurred attempting to encode the message: %s\n", strerror(errno));
    }

    assert(total_bytes_read == message_size);
}

void encode_header_data(struct png_handler * png_handler, const char * output_filename, char * message_filename, const long message_size) {
    char header_data[1024];
    const unsigned long header_size = HEADER_STATIC_SIZE + strlen(message_filename) + count_digits(message_size);

    snprintf(header_data, 1024, "png_%s_%lu_", message_filename, message_size);
    header_data[header_size] = '\0';

    encode_png(output_filename, header_data, header_size, png_handler);
}

int handle_encode(const struct cmd_arguments * arguments) {
    int return_value = verify_arguments(arguments);
    if (return_value == -1) {
        usage();
    }

    struct png_handler * png_handler = malloc(sizeof(struct png_handler));
    if (!png_handler) {
        return -1;
    }

    return_value = load_png_file(arguments->png_path, arguments->copy_png_path, png_handler);
    if (return_value == -1) {
        destroy_png_handler(png_handler);
        free(png_handler);
        return -1;
    }

    const long message_size = get_file_size_bytes(arguments->message_path);
    if (message_size == -1) {
        destroy_png_handler(png_handler);
        free(png_handler);
        return -1;
    }
    if (message_size == 0) {
        fprintf(stderr, "The message file doesn't contain any data");
        destroy_png_handler(png_handler);
        free(png_handler);
        return -1;
    }

    return_value = validate_png_size(png_handler, message_size);
    if (return_value == -1) {
        destroy_png_handler(png_handler);
        free(png_handler);
        return -1;
    }

    // encode_data(png_handler, arguments, message_size);
    encode_header_data(png_handler, arguments->copy_png_path, arguments->message_path, message_size);

    // destroy_png_handler(png_handler);
    free(png_handler);

    return 0;
}
