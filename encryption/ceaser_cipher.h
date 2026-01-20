#ifndef PNG_STEGANOGRAPHY_CEASER_CIPHER_H
#define PNG_STEGANOGRAPHY_CEASER_CIPHER_H


char * encrypt_message(int message_fd, long chunk_size, long ceaser_rotations, long * total_bytes_read);
char * decrypt_message(char * encrypted_message, long message_length, long ceaser_rotations);



#endif //PNG_STEGANOGRAPHY_CEASER_CIPHER_H