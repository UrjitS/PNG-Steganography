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

int set_last_bit(const int number, const bool bit) {
    if (bit) {
        return number | 1;
    }
    return number & ~1;
}

unsigned int encode_png(png_byte * row_pointers[], const char * message_chunk, const unsigned long message_chunk_size, const struct png_handler * png_handler, unsigned int start_pixel_index) {
    const int channels = png_get_channels(png_handler->png_ptr, png_handler->info_ptr);

    unsigned int pixel_index = start_pixel_index;
    const unsigned int total_pixels = png_handler->height * png_handler->width;

    for (int index = 0; index < message_chunk_size && pixel_index < total_pixels; index++) {
        const int character_ord = (unsigned char) message_chunk[index];

        for (int current_bit_index = 0; current_bit_index < 8 && pixel_index < total_pixels; current_bit_index += 2, pixel_index++) {
            const unsigned int row = pixel_index / png_handler->width;
            const unsigned int col = pixel_index % png_handler->width;
            const unsigned int pixel_offset = col * channels;

            row_pointers[row][pixel_offset + 1] = set_last_bit(row_pointers[row][pixel_offset + 1], get_bit(character_ord, current_bit_index));
            row_pointers[row][pixel_offset + 2] = set_last_bit(row_pointers[row][pixel_offset + 2], get_bit(character_ord, current_bit_index + 1));
        }
    }

    return pixel_index;
}


void encode_data(png_byte * row_pointers[], const struct png_handler * png_handler, const struct cmd_arguments * arguments, const long message_size, const unsigned int start_pixel_index) {
    char * chunk;
    long total_bytes_read = 0;
    long last_bytes_read = 0;
    unsigned int pixel_index = start_pixel_index;

    errno = 0;

    while ((chunk = encrypt_message(arguments->message_fd, 512, arguments->ceaser_rotations, &total_bytes_read)) != NULL) {
        pixel_index = encode_png(row_pointers, chunk, (total_bytes_read - last_bytes_read), png_handler, pixel_index);
        last_bytes_read = total_bytes_read;
        free(chunk);
    }

    if (errno != 0) {
        fprintf(stderr, "An error occurred attempting to encode the message: %s\n", strerror(errno));
    }

    assert(total_bytes_read == message_size);
}

unsigned int encode_header_data(png_byte * row_pointers[], const struct png_handler * png_handler, char * message_filename, const long message_size, const long ceaser_rotations) {
    char header_data[1024];
    const unsigned long header_size = HEADER_STATIC_SIZE + strlen(message_filename) + count_digits(message_size);

    snprintf(header_data, 1024, "png_%s_%lu_", message_filename, message_size);
    header_data[header_size] = '\0';

    // Encrypt the header data
    for (unsigned long index = 0; index < header_size; ++index) {
        header_data[index] = header_data[index] + ceaser_rotations;
    }

    return encode_png(row_pointers, header_data, header_size, png_handler, 0);
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

    png_byte * row_pointers[png_handler->height];

    for (int row = 0; row < png_handler->height; row++)
        row_pointers[row] = nullptr;

    for (int row = 0; row < png_handler->height; row++) {
        row_pointers[row] = png_malloc(png_handler->png_ptr, png_get_rowbytes(png_handler->png_ptr, png_handler->info_ptr));
    }

    png_read_image(png_handler->png_ptr, row_pointers);

    const unsigned int header_end_pixel = encode_header_data(row_pointers, png_handler, arguments->message_path, message_size, arguments->ceaser_rotations);

    encode_data(row_pointers, png_handler, arguments, message_size, header_end_pixel);

    fclose(png_handler->png_file_ptr);
    png_destroy_read_struct(&png_handler->png_ptr, &png_handler->info_ptr, nullptr);

    save_png(arguments->copy_png_path, row_pointers, png_handler);

    for (int row = 0; row < png_handler->height; row++) {
        free(row_pointers[row]);
    }

    free(png_handler);
    return 0;
}
