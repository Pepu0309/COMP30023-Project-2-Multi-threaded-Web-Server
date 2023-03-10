//
// Created by User on 14/5/2022.
//
#include "respond.h"

// A function which has an argument representing the socket to send a message to and the message. Calls syscall write()
// to send the message to the socket. Returns false in the case of an error; returns true otherwise.
// The if(write_message() == WRITE_ERROR) statement is used across functions in this module to check if an error
// occurred in write_message. In which case, the thread returns immediately back up to the serve_connection() function
// which will then close the socket (drop the connection), free all memory and terminate itself.
bool write_message(int sockfd_to_send, char *message) {
    // Write message back
    int n = write(sockfd_to_send, message, strlen(message));
    if (n < 0) {
        perror("write");
        return WRITE_ERROR;
    }
    return WRITE_SUCCESSFUL;
}

// Function which has an argument representing the socket to send the HTTP response back to as well as the file_path
// derived from the incoming HTTP request. This function does several checks to determine that the file_path is
// valid and then writes an appropriate HTTP response depending on the circumstances. If a write error occurs or a
// sendfile error occurs, this function will immediately exit by returning and have serve_connection close the socket
// and free the memory as usual.
void send_http_response(int sockfd_to_send, char *file_path) {
    int file_path_fd;

    // stat struct from standard library which will allow access to the file size
    struct stat file_stat;

    // If the file we're trying to read from does not exist, open will return -1 as per the linux manual located at
    // https://man7.org/linux/man-pages/man2/open.2.html. Hence, if we cannot open what is located at the file path
    // then we return a 404.
    if((file_path_fd = open(file_path, O_RDONLY)) < 0 ) {
        if(write_message(sockfd_to_send, "HTTP/1.0 404 Not Found\r\n\r\n") == WRITE_ERROR) {
            return;
        }
        // Otherwise, the file exists, and we can use fstat to get the statistics of it.
    } else {
        /* Call fstat on file_path to get the statistics of the file located at file_path and then store it in the
           stat struct file_stat declared earlier. */
        fstat(file_path_fd, &file_stat);

        // Test that the file_path leads to a regular file and not something else like a directory. The S_ISREG macro
        // comes from the linux manual page, https://man7.org/linux/man-pages/man7/inode.7.html
        if(S_ISREG(file_stat.st_mode)) {
            // Write to indicate a successful get response.
            if(write_message(sockfd_to_send, "HTTP/1.0 200 OK\r\n") == WRITE_ERROR) {
                return;
            }

            // Write the Content-Type header first without sending the actual MIME content type
            if(write_message(sockfd_to_send, "Content-Type: ") == WRITE_ERROR) {
                return;
            }

            if(write_content_type(sockfd_to_send, file_path) == WRITE_ERROR) {
                return;
            }

            // CRLF to terminate the Content-Type header line and then another CRLF to indicate the end of the headers.
            if(write_message(sockfd_to_send, "\r\n\r\n") == WRITE_ERROR) {
                return;
            }

            off_t file_to_send_size = file_stat.st_size;
            off_t total_num_bytes_sent = 0;
            off_t bytes_successfully_sent = 0;

            // Benefits of sendfile(): sendfile() does it's copying from file to file in the kernel instead of the user
            // space which is more efficient. User space operations such as read() and write() are I/O operations which
            // require a system call as we were taught in the earlier weeks of the subject. Furthermore, as we were
            // taught before (or explored during a tute with the tutor), doing a system call is quite expensive and
            // hence why sendfile() is faster. This is reflected in the Linux manual page
            // https://man7.org/linux/man-pages/man2/sendfile.2.html. Furthermore, in terms of code
            // simplicity, there is no need to do separate calls to read the file and write the contents to the socket.
            // There would also be no need to declare or size a buffer with consideration of the file size and to
            // concatenate the file contents to the buffer if the approach was to have everything in a buffer and
            // write it all at once.

            // Track the bytes sent by sendfile() and make sure that all bytes are sent.
            while(total_num_bytes_sent < file_to_send_size) {
                // sendfile returns -1 in the case of an error or the number of bytes successfully sent as per the
                // linux manual located at https://man7.org/linux/man-pages/man2/sendfile.2.html.
                bytes_successfully_sent = sendfile(sockfd_to_send, file_path_fd,
                                                   &total_num_bytes_sent, file_to_send_size);
                // If there was no error, then we increment the total number of bytes sent.
                if(bytes_successfully_sent >= 0) {
                    total_num_bytes_sent += bytes_successfully_sent;
                } else if (bytes_successfully_sent < 0) {
                    return;
                }
            }
        // Otherwise, we send back a 404 not found response as well if the file_path does not lead to a regular file.
        } else {
            if(write_message(sockfd_to_send, "HTTP/1.0 404 Not Found\r\n\r\n") == WRITE_ERROR) {
                return;
            }
        }

    }

}

// A function that is responsible for determining the content type and calling write_message to write it. If at any
// point a write error occurs, then the function propagates the write error up the call stack to send_http_response.
bool write_content_type(int sockfd_to_send, char *file_path) {
    char *extension;

    // Use strrchr to get the last occurrence of the FILE_EXTENSION_DELIMITER which is the '.' character. This deals
    // with "false" extensions in the file_path. Handling '.' characters that are not associated with an extension
    // is handled below.
    extension = strrchr(file_path, FILE_EXTENSION_DELIMITER);

    // If there is a '.' character found in the file_path
    if(extension != NULL) {
        // Among the four MIME content type the server identifies, if any of them are found, then return the MIME
        // content type as specified by https://mimetype.io/all-types/.
        if(strcmp(extension, HTML_EXTENSION) == SAME_STRING) {
            if(write_message(sockfd_to_send, "text/html") == WRITE_ERROR) {
                return WRITE_ERROR;
            }
        } else if (strcmp(extension, JPEG_EXTENSION) == SAME_STRING) {
            if(write_message(sockfd_to_send, "image/jpeg") == WRITE_ERROR) {
                return WRITE_ERROR;
            }
        } else if (strcmp(extension, JAVA_SCRIPT_EXTENSION) == SAME_STRING) {
            if(write_message(sockfd_to_send, "text/javascript") == WRITE_ERROR) {
                return WRITE_ERROR;
            }
        } else if (strcmp(extension, CSS_EXTENSION) == SAME_STRING) {
            if(write_message(sockfd_to_send, "text/css") == WRITE_ERROR) {
                return WRITE_ERROR;
            }
        // If there is a '.' character found in the file path, but it's either a file extension not part of
        // the four or part of something else in the file path which is not
        // a file extension (which we don't care about).
        } else {
            if(write_message(sockfd_to_send, "application/octet-stream") == WRITE_ERROR) {
                return WRITE_ERROR;
            }
        }
    // If there is no '.' character found which means no file extension.
    } else {
        if(write_message(sockfd_to_send, "application/octet-stream") == WRITE_ERROR) {
            return WRITE_ERROR;
        }
    }
    return WRITE_SUCCESSFUL;
}

