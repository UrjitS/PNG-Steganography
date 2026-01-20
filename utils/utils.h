#ifndef PNG_STEGANOGRAPHY_UTILS_H
#define PNG_STEGANOGRAPHY_UTILS_H

#include <png.h>

struct png_handler {
    FILE * png_file_ptr;
    png_structp png_ptr;
    png_structp write_png_ptr;
    png_infop info_ptr;
    png_infop write_info_ptr;
    png_infop end_info;

    png_uint_32 height;
    png_uint_32 width;
    int bit_depth;
    int color_type;
};

struct cmd_arguments {
    char * message_path;
    int message_fd;
    char * png_path;
    char * copy_png_path; //TODO free memory
    long ceaser_rotations;
    bool decode_flag;
    bool given_rotations;
};

int load_png_file(const char * original_png_path, const char * cpy_png_path, struct png_handler * png_handler);
void save_png(const char * cpy_png_path, png_byte * row_pointers[], struct png_handler * png_handler);
int validate_png_size(struct png_handler * png_handler, long message_size);
long get_file_size_bytes(const char * file_path);
void destroy_png_handler(struct png_handler * png_handler);
int count_digits(long number);
[[noreturn]] void usage();

#endif //PNG_STEGANOGRAPHY_UTILS_H