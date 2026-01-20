#include "ceaser_cipher.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


char * encrypt_message(const int message_fd, const long chunk_size, const long ceaser_rotations, long * total_bytes_read) {
    char * message_buffer = malloc(chunk_size);
    if (message_buffer == NULL) {
        return nullptr;
    }

    const ssize_t read_bytes = read(message_fd, message_buffer, chunk_size);
    if (read_bytes == 0) {
        return nullptr;
    }
    if (read_bytes == -1) {
        fprintf(stderr, "Read encountered an error\n");
        return nullptr;
    }

    *total_bytes_read += read_bytes;
    for (int index = 0; index < read_bytes; ++index) {
        message_buffer[index] = message_buffer[index] + ceaser_rotations;
    }

    return message_buffer;
}


char * decrypt_message(char * encrypted_message, const long message_length, const long ceaser_rotations) {
    for (int index = 0; index < message_length; ++index) {
        encrypted_message[index] = encrypted_message[index] - ceaser_rotations;
    }

    return encrypted_message;
}

