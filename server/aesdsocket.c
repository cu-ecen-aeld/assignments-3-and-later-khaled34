/*--------------------------------- Private includes ---------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <syslog.h>
/*--------------------------------- Private definitions ---------------------------------  */
#define PORT                                    "9000"
#define BACKLOG                                 10
#define MAXDATASIZE                             1024
#define FILE_PATH                               "/var/tmp/aesdsocketdata"
#define UNINIT_VALUE                            -1
/*---------------------------------- Private Variables ----------------------------------  */
static int data_packet_fd = UNINIT_VALUE;
static int server_socket_fd = UNINIT_VALUE;
/*--------------------------------- Private Functions ---------------------------------  */
/**
 * @brief A special signal handler to clean up the system when SIGTERM or SIGINT is initiated
 * 
 * @param signo     [IN]  signal value
 * 
 * @return None
 * 
 */
static void special_signal_handler(int signo) 
{
    syslog(LOG_DEBUG, "Caught signal, exiting");

    if (data_packet_fd != UNINIT_VALUE) 
    {
        close(data_packet_fd);
        unlink(FILE_PATH);
    }
    if (server_socket_fd != UNINIT_VALUE) 
    {
        close(server_socket_fd);
    }

    closelog();
    exit(0);
}

/**
 * @brief A helper function used to return the socket IPv based address
 * 
 * @param sa     [IN]  Pointer to sockaddr structure
 * 
 * @return pointer to sin_addr in case of IPv4 and sin6_addr in IPv6
 * 
 */
static void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
/**
 * @brief check if -d option is passed in the parameters if exists it demonize the server process
 * 
 * @param argc     [IN]  number of arguments
 * @param argv     [IN]  array of pointers to strings passed in arguments execution
 * 
 * @return status indicating the correctness of daemonization
 * 
 */
static int check_and_handle_daemon_option(int argc, char** argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) 
    {
        if (opt == 'd') 
        {
            if (daemon(0, 0) == -1) 
            {
                syslog(LOG_ERR, "Daemon failed\n");
                return EXIT_FAILURE;
            }
        }
        else 
        {
            syslog(LOG_ERR, "Invalid arguments\n");
            return EXIT_FAILURE;
        }
    }
    return 0;
}

/**
 * @brief Server main function 
 */
static void run_server()
{
    int accepted_fd;
    struct addrinfo hints, *servinfo, *res_indx_ptr;
    struct sockaddr_storage client_addr;
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    char buf[MAXDATASIZE];
    char *packet_buffer = NULL;
    size_t packet_buffer_size = 0;
    size_t packet_buffer_capacity = MAXDATASIZE;

    // Open/Create the file for appending data
    data_packet_fd = open(FILE_PATH, O_CREAT | O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
    if (data_packet_fd == -1) 
    {
        syslog(LOG_ERR, "Error opening/creating the file\n");
        exit(EXIT_FAILURE);
    }

    // Set up address info for binding
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) 
    {
        syslog(LOG_ERR, "getaddrinfo failed\n");
        exit(EXIT_FAILURE);
    }

    // Bind to the first available address
    for(res_indx_ptr = servinfo; res_indx_ptr != NULL; res_indx_ptr = res_indx_ptr->ai_next)
    {
        if ((server_socket_fd = socket(res_indx_ptr->ai_family, res_indx_ptr->ai_socktype, res_indx_ptr->ai_protocol)) == -1)
        {
            syslog(LOG_ERR, "server: socket failed\n");
            continue;
        }

        int yes = 1;
        if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) 
        {
            syslog(LOG_ERR, "setsockopt failed\n");
            close(server_socket_fd);
            continue;
        }

        if (bind(server_socket_fd, res_indx_ptr->ai_addr, res_indx_ptr->ai_addrlen) == -1) 
        {
            close(server_socket_fd);
            syslog(LOG_ERR, "server: bind failed\n");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if (res_indx_ptr == NULL)  
    {
        syslog(LOG_ERR, "server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket_fd, BACKLOG) == -1) 
    {
        syslog(LOG_ERR, "listen failed\n");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "server: waiting for connections...\n");

    // Main server loop
    while(1) 
    {
        sin_size = sizeof client_addr;
        accepted_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &sin_size);
        if (accepted_fd == -1) 
        {
            syslog(LOG_ERR, "accept error\n");
            continue;
        }

        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), s, sizeof s);
        syslog(LOG_INFO, "Accepted connection from %s\n", s);

        packet_buffer = malloc(MAXDATASIZE);
        if (packet_buffer == NULL) 
        {
            syslog(LOG_ERR, "malloc failed\n");
            close(accepted_fd);
            continue;
        }
        packet_buffer_size = 0;
        packet_buffer_capacity = MAXDATASIZE;

        ssize_t recv_octets;
        while ((recv_octets = recv(accepted_fd, buf, MAXDATASIZE - 1, 0)) > 0) 
        {
            buf[recv_octets] = '\0'; // Null-terminate the received data

            // Resize the packet buffer if necessary
            if (packet_buffer_size + recv_octets >= packet_buffer_capacity) 
            {
                size_t required_capacity = packet_buffer_size + recv_octets + 1; // +1 for null terminator
                char *new_buffer = realloc(packet_buffer, required_capacity);
                if (new_buffer == NULL) 
                {
                    syslog(LOG_ERR, "realloc failed\n");
                    free(packet_buffer);
                    close(accepted_fd);
                    exit(EXIT_FAILURE);
                }
                packet_buffer = new_buffer;
                packet_buffer_capacity = required_capacity;
            }

            // Append the received data to the packet buffer
            memcpy(packet_buffer + packet_buffer_size, buf, recv_octets);
            packet_buffer_size += recv_octets;

            // Check if the received data contains a newline
            char *newline_pos = memchr(packet_buffer, '\n', packet_buffer_size);
            if (newline_pos != NULL) 
            {
                // Write the received data (including the newline) to the file
                ssize_t num_written_octets = write(data_packet_fd, packet_buffer, newline_pos - packet_buffer + 1);
                if (num_written_octets == -1) 
                {
                    syslog(LOG_ERR, "Error Writing in the file\n"); 
                    free(packet_buffer);
                    close(accepted_fd);
                    exit(EXIT_FAILURE);
                }

                // Send the full content of the file back to the client
                lseek(data_packet_fd, 0, SEEK_SET); // Reset file pointer to the beginning
                char file_buf[MAXDATASIZE];
                ssize_t read_octets;
                while ((read_octets = read(data_packet_fd, file_buf, MAXDATASIZE - 1)) > 0) 
                {
                    file_buf[read_octets] = '\0'; // Null-terminate the file data
                    send(accepted_fd, file_buf, read_octets, 0);
                }

                // Reset the packet buffer
                packet_buffer_size = 0;
            }
        }

        free(packet_buffer);
        syslog(LOG_INFO, "Closed connection from %s\n", s);
        close(accepted_fd);
    }
}

int main(int argc, char** argv)
{
    // Set up signal handling
    signal(SIGTERM, special_signal_handler);
    signal(SIGINT, special_signal_handler);

    // Open syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Handle daemon option
    if (check_and_handle_daemon_option(argc, argv) != 0) 
    {
        exit(EXIT_FAILURE);
    }

    // Run the server
    run_server();

    // Clean up
    closelog();
    return 0;
}