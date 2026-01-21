#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "encode.h"
#include "decode.h"
#include "utils/utils.h"

void parse_arguments(const int argc, char * argv[], struct cmd_arguments * arguments) {
    int opt;

    opterr = 0;

    while ((opt = getopt(argc, argv, ":dm:p:r:")) != -1) {
        switch (opt) {
            case 'd':
                arguments->decode_flag = true;
                break;
            case 'm':
                arguments->message_path = optarg;
                break;
            case 'p':
                arguments->png_path = optarg;
                break;
            case 'r':
                errno = 0;
                char * end;
                const long value = strtol(optarg, &end, 10);

                if (errno != 0 || *end != '\0') {
                    fprintf(stderr, "Invalid number: %s\n", optarg);
                    usage();
                }
                arguments->ceaser_rotations = value;
                arguments->given_rotations = true;
                break;
            case  ':':
                fprintf(stderr, "Option needs a value %s \n", optarg);
                usage();
            case '?':
                fprintf(stderr, "Unknown argument\n");
                usage();
            default:
                usage();
        }
    }

    if(optind < argc - 1)
    {
        fprintf(stderr, "Too many arguments provided\n");
        usage();
    }
}

int handle_arguments(struct cmd_arguments * arguments) {
    if (!arguments->decode_flag) {
        // Check to see if the message file exists
        const int message_fd = open(arguments->message_path, O_RDONLY);
        if (message_fd == -1) {
            fprintf(stderr, "Failed to open message file: %s", strerror(errno));
            return -1;
        }
        arguments->message_fd = message_fd;
        arguments->copy_png_path = "./output.png";
    }

    // Check to see if the PNG file exists
    const int png_fd = open(arguments->png_path, O_RDONLY);
    if (png_fd == -1) {
        fprintf(stderr, "Failed to open png file: %s", strerror(errno));
        return -1;
    }

    close(png_fd);

    // Check to see if the PNG is a PNG
    if (strstr(arguments->png_path, ".png") == NULL) {
        fprintf(stderr, "The provided png file is not a png\n");
        return -1;
    }

    return 0;
}

int main (const int argc, char * argv[]) {
    struct cmd_arguments * arguments = malloc(sizeof(struct cmd_arguments));
    if (!arguments) {
        fprintf(stderr, "Failed to allocate memory for struct cmd_arguments");
        return EXIT_FAILURE;
    }

    arguments->given_rotations = false;
    arguments->decode_flag = false;

    parse_arguments(argc, argv, arguments);
    int return_value = handle_arguments(arguments);
    if (return_value == -1) {
        free(arguments);
        return EXIT_FAILURE;
    }

    if (arguments->decode_flag) {
        return_value = handle_decode(arguments);
    } else {
        return_value = handle_encode(arguments);
    }

    if (return_value == -1) {
        if (!arguments->decode_flag) {
            close(arguments->message_fd);
        }
        free(arguments);
        return EXIT_FAILURE;
    }


    if (!arguments->decode_flag) {
        close(arguments->message_fd);
    }
    free(arguments);
    return EXIT_SUCCESS;
}