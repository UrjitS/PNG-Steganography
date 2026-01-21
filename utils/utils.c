#include "utils.h"

#include <png.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

// https://www.libpng.org/pub/png/libpng-1.4.0-manual.pdf

void create_png_copy(const char * original_png_path, const char * new_file_name) {
    int character;

    FILE *fptr1 = fopen(original_png_path, "r");
    if (fptr1 == NULL)
    {
        printf("Cannot open file %s\n", original_png_path);
        exit(1);
    }

    FILE *fptr2 = fopen(new_file_name, "w");
    if (fptr2 == NULL)
    {
        printf("Cannot open file %s\n", new_file_name);
        exit(1);
    }

    while ((character = fgetc(fptr1)) != EOF)
    {
        fputc(character, fptr2);
    }

    printf("Contents copied to %s\n", new_file_name);

    fclose(fptr1);
    fclose(fptr2);
}

void save_png(const char * cpy_png_path, png_byte * row_pointers[], struct png_handler * png_handler) {
    png_handler->png_file_ptr = fopen(cpy_png_path, "wb");
    if (!png_handler->png_file_ptr) {
        fprintf(stderr, "Could not open file for writing\n");
        png_destroy_write_struct(&png_handler->write_png_ptr, &png_handler->write_info_ptr);
    }

    png_init_io(png_handler->write_png_ptr, png_handler->png_file_ptr);

    png_set_IHDR(png_handler->write_png_ptr, png_handler->write_info_ptr,
                 png_handler->width, png_handler->height, png_handler->bit_depth, png_handler->color_type,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_handler->write_png_ptr, png_handler->write_info_ptr);
    png_write_image(png_handler->write_png_ptr, row_pointers);
    png_write_end(png_handler->write_png_ptr, nullptr);

    // Cleanup
    fclose(png_handler->png_file_ptr);
    png_destroy_write_struct(&png_handler->write_png_ptr, &png_handler->write_info_ptr);
}

int load_png_file(const char * original_png_path, const char * cpy_png_path, struct png_handler * png_handler) {
    const png_byte * buffer = malloc(10);
    create_png_copy(original_png_path, cpy_png_path);
    png_handler->png_file_ptr = fopen(cpy_png_path, "rb+");

    if (!png_handler->png_file_ptr) {
        fprintf(stderr, "Could not open the PNG file\n");
        free((void * )buffer);
        return -1;
    }

    fread((void * )buffer, 1, 8, png_handler->png_file_ptr);

    const bool is_png = !png_sig_cmp(buffer, 0, 8);
    if (!is_png)
    {
        fprintf(stderr, "The png file provided is not a PNG\n");
        free((void * )buffer);
        return -1;
    }

    png_handler->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, nullptr, nullptr);
    if (!png_handler->png_ptr) {
        fprintf(stderr, "Could not create png ptr\n");
        free((void * )buffer);
        return -1;
    }

    png_handler->write_png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, nullptr, nullptr);
    if (!png_handler->write_png_ptr) {
        fprintf(stderr, "Could not create png ptr\n");
        free((void * )buffer);
        return -1;
    }

    png_handler->info_ptr = png_create_info_struct(png_handler->png_ptr);
    if (!png_handler->info_ptr)
    {
        png_destroy_read_struct(&png_handler->png_ptr, nullptr, nullptr);
        fprintf(stderr, "Could not create info_ptr\n");
        free((void * )buffer);
        return -1;
    }

    png_handler->write_info_ptr = png_create_info_struct(png_handler->write_png_ptr);

    if (!png_handler->write_info_ptr)
    {
        png_destroy_read_struct(&png_handler->write_png_ptr, nullptr, nullptr);
        fprintf(stderr, "Could not create info_ptr\n");
        free((void * )buffer);
        return -1;
    }

    png_handler->end_info = png_create_info_struct(png_handler->png_ptr);
    if (!png_handler->end_info)
    {
        png_destroy_read_struct(&png_handler->png_ptr, &png_handler->info_ptr,nullptr);
        fprintf(stderr, "Could not create end_info\n");
        free((void * )buffer);
        return -1;
    }

    png_init_io(png_handler->png_ptr, png_handler->png_file_ptr);
    png_init_io(png_handler->write_png_ptr, png_handler->png_file_ptr);

    png_set_sig_bytes(png_handler->png_ptr, 8);

    png_read_info(png_handler->png_ptr, png_handler->info_ptr);

    // Expand images to 8 bits per channel
    if (png_get_bit_depth(png_handler->png_ptr, png_handler->info_ptr) < 8) {
        png_set_packing(png_handler->png_ptr);
    }

    // Convert palette to RGB
    if (png_get_color_type(png_handler->png_ptr, png_handler->info_ptr) == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_handler->png_ptr);
    }

    // Expand grayscale to RGB if needed
    if (png_get_color_type(png_handler->png_ptr, png_handler->info_ptr) == PNG_COLOR_TYPE_GRAY || png_get_color_type(png_handler->png_ptr, png_handler->info_ptr) == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_handler->png_ptr);
    }

    // Update info_ptr with the transformations
    png_read_update_info(png_handler->png_ptr, png_handler->info_ptr);

    free((void * )buffer);

    return 0;
}

int validate_png_size(struct png_handler * png_handler, const long message_size) {
    png_handler->height = png_get_image_height(png_handler->png_ptr, png_handler->info_ptr);
    png_handler->width = png_get_image_width(png_handler->png_ptr, png_handler->info_ptr);
    png_handler->bit_depth = png_get_bit_depth(png_handler->png_ptr, png_handler->info_ptr);
    png_handler->color_type = png_get_color_type(png_handler->png_ptr, png_handler->info_ptr);
    const png_byte interlace = png_get_interlace_type(png_handler->png_ptr, png_handler->info_ptr);

    fprintf(stdout, "Image width: %d\n", png_handler->width);
    fprintf(stdout, "Image height: %d\n", png_handler->height);

    const png_uint_32 bit_depth = png_get_bit_depth(png_handler->png_ptr, png_handler->info_ptr);
    fprintf(stdout, "Image bit depth: %d\n", bit_depth);
    fprintf(stdout, "PNG Interlace: %d\n", interlace);

    const long number_of_pixels = png_handler->height * png_handler->width;

    // Each pixel stores 2 bits (1 bit in Green channel LSB + 1 bit in Blue channel LSB)
    // Each byte of message needs 8 bits = 4 pixels
    const long png_capacity = number_of_pixels / 4;

    fprintf(stdout, "PNG capacity: %lu bytes\n", png_capacity);
    fprintf(stdout, "Message size: %lu bytes\n", message_size);

    if (png_capacity < message_size) {
        fprintf(stdout, "The provided PNG cannot fit the message contents");
        return -1;
    }

    return 0;
}

long get_file_size_bytes(const char * file_path) {
    struct stat file_stats;
    const int ret_status = stat(file_path, &file_stats);
    if (ret_status == -1) {
        fprintf(stderr, "Could not get the file stats for the given file path %s", file_path);
        return -1;
    }
    return file_stats.st_size;
}

void destroy_png_handler(struct png_handler * png_handler) {
    png_destroy_info_struct(png_handler->png_ptr, &png_handler->info_ptr);
    png_destroy_read_struct(&png_handler->png_ptr, &png_handler->info_ptr, &png_handler->end_info);
    fclose(png_handler->png_file_ptr);
}

int count_digits(long number) {
    if (number == 0) {
        return 1;
    }

    int count = 0;
    while (number != 0) {
        number = number / 10;
        count++;
    }

    return count;
}

[[noreturn]] void usage() {
    printf("\n----PNG Steganography----\n");
    printf("Arguments:\n");
    printf("\t -m <message file path> -p <png file path> -r <ceaser cipher rotation number>\n");
    exit(EXIT_FAILURE);
}