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
#include <pthread.h>
#if (QUEUE_BSD_LINKED)
#include <queue.h>
#endif //QUEUE_BSD_LINKED
#include <time.h>
#include "../aesd-char-driver/aesd_ioctl.h"
/*--------------------------------- Private definitions ---------------------------------  */
#define PORT                                    "9000"
#define BACKLOG                                 10
#define MAXDATASIZE                             1024
#define UNINIT_VALUE                            -1

#define USE_AESD_CHAR_DEVICE                    1

#if (!USE_AESD_CHAR_DEVICE)
#define FILE_PATH                               "/var/tmp/aesdsocketdata"
#else
#define FILE_PATH                               "/dev/aesdchar"
#define AESD_SEEKTO_COMMAND                     "AESDCHAR_IOCSEEKTO:" 
#define AESD_SEEKTO_PIVOT_LEN                   19
#endif /*(!USE_AESD_CHAR_DEVICE)*/


/**
 * @brief Node definition 
 */
typedef struct client_thread {
    pthread_t thread_id;
    int client_fd;
    int complete;
#if (QUEUE_BSD_LINKED)
    TAILQ_ENTRY(client_thread) entries;
#else
    struct client_thread * nxt_node;
#endif 
} client_thread_t;
/*---------------------------------- Private Variables ----------------------------------  */
static int data_packet_fd = UNINIT_VALUE;
static int server_socket_fd = UNINIT_VALUE;
static volatile sig_atomic_t shutdown_requested = 0;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;
#if (QUEUE_BSD_LINKED)
static TAILQ_HEAD(client_thread_list, client_thread) thread_list;
#else
static client_thread_t* thread_list;
#endif
/*--------------------------------- Private Functions ---------------------------------  */
void insert_at_end(client_thread_t** head, client_thread_t* new_node) 
{
    if (!new_node) return; // Ensure the new node is valid

    if (*head == NULL) 
    {
        *head = new_node;
        return;
    }

    client_thread_t* temp = *head;

    while (temp->nxt_node != NULL) 
    {
        temp = temp->nxt_node;
    }
    temp->nxt_node = new_node;
}
client_thread_t* detect_and_rmv_node(client_thread_t** head, client_thread_t* node_to_remove) 
{
    client_thread_t* nxt_node_client = NULL;

    if (*head == NULL || node_to_remove == NULL)
    {
        goto func_exit;
    }

    // If the node to remove is the head
    if (*head == node_to_remove) 
    {
        *head = node_to_remove->nxt_node;
        free(node_to_remove);
        nxt_node_client = *head;
        goto func_exit;
    }

    // Find the node before the node to remove
    client_thread_t* temp = *head;
    while (temp->nxt_node != NULL && temp->nxt_node != node_to_remove) 
    {
        temp = temp->nxt_node;
    }

    // If node was found in the list
    if (temp->nxt_node == node_to_remove) 
    {
        temp->nxt_node = node_to_remove->nxt_node;
        free(node_to_remove);
        nxt_node_client = temp->nxt_node;
    }
func_exit:
    return nxt_node_client;
}

#if  (!USE_AESD_CHAR_DEVICE)
/**
 * @brief timer signal handler 
 * 
 * @param signo [IN]: the signal received
 * 
 * 
 */void timer_handler(int signo) 
{
    if (signo == SIGALRM) 
    {
        syslog(LOG_DEBUG, "SIGALARM signal caught\n");
        char timestamp[128];
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        // Format timestamp as RFC 2822
        strftime(timestamp, sizeof(timestamp), "timestamp: %a, %d %b %Y %H:%M:%S %z\n", tm_info);

        // Append to file
        pthread_mutex_lock(&file_mutex);
        write(data_packet_fd, timestamp, strlen(timestamp));
        pthread_mutex_unlock(&file_mutex);

    }
}

/**
 * @brief Function to initialize the timer and start it 
 * 
 */
void start_timer(void) 
{
    struct sigevent sev;
    struct itimerspec its;
    timer_t timerid;
    struct sigaction sa;

    // Set up signal handler for SIGALRM
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    // Create a timer that triggers SIGALRM
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_ptr = &timerid;
    timer_create(CLOCK_REALTIME, &sev, &timerid);

    // Configure the timer to fire every 10 seconds
    its.it_value.tv_sec = 10;  // Initial expiration
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 10;  // Repeats every 10 seconds
    its.it_interval.tv_nsec = 0;

    // Start the timer
    timer_settime(timerid, 0, &its, NULL);
}
#endif /*(!USE_AESD_CHAR_DEVICE)*/
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

    if (signo == SIGTERM || signo == SIGINT) {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        shutdown_requested = 1;
        // Force accept() to return with an error to handle the shutdown_requested
        close(server_socket_fd); 
    }
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
 * @brief Helper function to check and resize the buffer used for recv on the socket
 * @param packet_buffer              [IN/OUT]  pointer to the packet_buffer which may be reallocated 
 * @param packet_buffer_capacity     [IN/OUT]  pointer to the current capacity of the buffer pointed to by packet_buffer
 * @param consumed_buffer_size       [IN]  Number of octets consumed in the buffer
 * @param recv_octets                [IN]  total number of received octets from recv call
 * 
 * @return status indicating the correctness of resize if needed
 * 
 */
static int check_and_resize_buffer(char **packet_buffer, size_t *packet_buffer_capacity, size_t consumed_buffer_size, size_t recv_octets) 
{
    if (consumed_buffer_size + recv_octets >= *packet_buffer_capacity) 
    {
        size_t required_capacity = consumed_buffer_size + recv_octets + 1;
        char *new_buffer = realloc(*packet_buffer, required_capacity);
        if (new_buffer == NULL) {
            syslog(LOG_ERR, "realloc failed\n");
            free(*packet_buffer);
            return -1;
        }
        *packet_buffer = new_buffer;
        *packet_buffer_capacity = required_capacity;
    }
    return 0;
}
#if (USE_AESD_CHAR_DEVICE)
static int Check_seekCmd(char * ptr_buff, int fd)
{
    int retval = EXIT_FAILURE;
    if (strncmp(ptr_buff, AESD_SEEKTO_COMMAND, AESD_SEEKTO_PIVOT_LEN) == 0) 
    {
        syslog(LOG_DEBUG, "[USER SPACE] Detected ICOTL COMMAND\n"); 
        char *pos_of_valid_digit = strchr(ptr_buff, ':');
        if (pos_of_valid_digit != NULL) 
        {
            /* Get the next value */
            pos_of_valid_digit++;
            
            char *endptr;
            uint32_t write_cmd = strtoul(pos_of_valid_digit, &endptr, 10);
            if (*endptr == ',') 
            {
                pos_of_valid_digit = endptr + 1; 
                uint32_t offset = strtoul(pos_of_valid_digit, &endptr, 10);
                syslog(LOG_DEBUG, "[USER SPACE] Detected ICOTL COMMAND offsets (%d,%d)\n", write_cmd, offset); 
                struct aesd_seekto seek_to = {write_cmd, offset};
                int ioctl_result = ioctl(fd, AESDCHAR_IOCSEEKTO, &seek_to);
                if (ioctl_result < 0) 
                {
                    syslog(LOG_ERR, "[USER SPACE] Error in calling the ioctl\n"); 
                    retval = EXIT_FAILURE;
                }
                retval = EXIT_SUCCESS;
            }
        }
    }
    return retval;
}
#endif

#if USE_AESD_CHAR_DEVICE
int open_device_file(char* ptr_file_path)
{
    return (open(ptr_file_path, O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO));
}
int close_device_file(int fd)
{
    return (close(fd));
}
#endif
/**
 * @brief client thread handler
 *        Main functionality is to receive and send data from the socket descriptor
 */
static void *handle_client(void *arg)
{
    client_thread_t *thread_node =  (client_thread_t *) arg;
    int accepted_fd = thread_node->client_fd;

    char buf[MAXDATASIZE];
    char *packet_buffer = NULL;
    size_t consumed_buffer_size = 0;
    size_t packet_buffer_capacity = MAXDATASIZE;
    
    packet_buffer = malloc(MAXDATASIZE);
    if (packet_buffer == NULL) 
    {
        syslog(LOG_ERR, "malloc failed\n");
        close(accepted_fd);
        return NULL;
    }
    ssize_t recv_octets;
    while ((recv_octets = recv(accepted_fd, buf, MAXDATASIZE - 1, 0)) > 0) 
    {
        syslog(LOG_INFO, "Inside the receive function\n");
        buf[recv_octets] = '\0';

        if ((check_and_resize_buffer(&packet_buffer, &packet_buffer_capacity, consumed_buffer_size, recv_octets)) == -1) 
        {
            close(accepted_fd);
            return NULL;
        }
        syslog(LOG_INFO, "MEMCOPY\n");
        memcpy(packet_buffer + consumed_buffer_size, buf, recv_octets);
        consumed_buffer_size += recv_octets;

        char *newline_pos = memchr(packet_buffer, '\n', consumed_buffer_size);
        if (newline_pos != NULL) 
        {
#if USE_AESD_CHAR_DEVICE
            /* open file */
            data_packet_fd = open_device_file(FILE_PATH);
            if (Check_seekCmd(packet_buffer, data_packet_fd) != EXIT_SUCCESS)
#endif
            {
                pthread_mutex_lock(&file_mutex);
                ssize_t num_written_octets = write(data_packet_fd, packet_buffer, newline_pos - packet_buffer + 1);
                pthread_mutex_unlock(&file_mutex);
                
                if (num_written_octets == -1) 
                {
                    syslog(LOG_ERR, "Error Writing in the file\n"); 
                    free(packet_buffer);
                    close(accepted_fd);
                    return NULL;
                }
                lseek(data_packet_fd, 0, SEEK_SET);
            }
            /* Read Back everything in the device */
            char file_buf[MAXDATASIZE];
            ssize_t read_octets;
            while ((read_octets = read(data_packet_fd, file_buf, MAXDATASIZE - 1)) > 0) 
            {
                file_buf[read_octets] = '\0';
                send(accepted_fd, file_buf, read_octets, 0);
            }
            consumed_buffer_size = 0;
#if USE_AESD_CHAR_DEVICE
            /* open file */
            close_device_file(data_packet_fd);
#endif
        }
    }
    
    free(packet_buffer);
    syslog(LOG_INFO, "Closed connection from client\n");
    close(accepted_fd);
    thread_node->complete = 1;
    return NULL;
}
/**
 * @brief Init server functionality till listen
 * 
 */
static int server_init(const char *port) 
{
    struct addrinfo hints, *servinfo, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &servinfo) != 0) 
    {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }

    for (res = servinfo; res != NULL; res = res->ai_next) 
    {
        server_socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (server_socket_fd == -1)
            continue;

        int yes = 1;
        setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(server_socket_fd, res->ai_addr, res->ai_addrlen) == 0)
            break;

        close(server_socket_fd);
    }
    freeaddrinfo(servinfo);

    if (res == NULL) {
        syslog(LOG_ERR, "Failed to bind");
        return -1;
    }
    if (listen(server_socket_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Listen failed");
        return -1;
    }
    return 0;
}

/**
 * @brief Server main thread
 * 
 */
static void run_server(const char *port, const char *file_path) 
{
#if (!QUEUE_BSD_LINKED)
    client_thread_t* client;
#endif
#if (!USE_AESD_CHAR_DEVICE)
    data_packet_fd = open(file_path, O_CREAT | O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
    if (data_packet_fd == -1) 
    {
        syslog(LOG_ERR, "Error opening/creating the file\n");
        exit(EXIT_FAILURE);
    }
#endif

    if (server_init(port) == -1)
    {
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Server waiting for connections...");
#if (QUEUE_BSD_LINKED)
    TAILQ_INIT(&thread_list);
#else
    thread_list = NULL;
#endif

    while (!shutdown_requested) {
        char s[INET6_ADDRSTRLEN];
        struct sockaddr_storage client_addr;
        socklen_t sin_size = sizeof client_addr;
        int client_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &sin_size);
        if (client_fd == -1)
        {
            continue;
        }
        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), s, sizeof s);
        syslog(LOG_INFO, "Accepted connection from %s\n", s);

        client_thread_t *new_client = malloc(sizeof(client_thread_t));
        if (!new_client) 
        {
            syslog(LOG_ERR, "Can't allocate memory for a thread\n");
            close(client_fd);
            continue;
        }
        // initialize the node 
        new_client->client_fd = client_fd;
        new_client->complete = 0;
        new_client->nxt_node = NULL;
        pthread_create(&new_client->thread_id, NULL, handle_client, new_client);
        pthread_mutex_lock(&thread_list_mutex);
#if (QUEUE_BSD_LINKED)
        TAILQ_INSERT_TAIL(&thread_list, new_client, entries);
#else
        insert_at_end(&thread_list, new_client);
#endif
        pthread_mutex_unlock(&thread_list_mutex);
        pthread_mutex_lock(&thread_list_mutex);
#if (QUEUE_BSD_LINKED)
        client_thread_t *client, *tmp;
        TAILQ_FOREACH_SAFE(client, &thread_list, entries, tmp) 
#else
        client      = thread_list; 
        while (client != NULL)
#endif
        {
            if (client->complete) 
            {
                pthread_join(client->thread_id, NULL);
#if (QUEUE_BSD_LINKED)
                TAILQ_REMOVE(&thread_list, client, entries);
                free(client);
            }
#else
                client = detect_and_rmv_node(&thread_list, client);

            }
            else
            {
                client = client->nxt_node;
            }
#endif
        }
        pthread_mutex_unlock(&thread_list_mutex);
    }
    /* Clean all nodes in the linked list */
    pthread_mutex_lock(&thread_list_mutex);
#if (QUEUE_BSD_LINKED)
    client_thread_t *client, *tmp;
    TAILQ_FOREACH_SAFE(client, &thread_list, entries, tmp) 
#else 
    client      = thread_list; 
    while (client != NULL)
#endif
    {
        pthread_join(client->thread_id, NULL);
#if (QUEUE_BSD_LINKED)
        TAILQ_REMOVE(&thread_list, client, entries);
        free(client);
#else
        
        client = detect_and_rmv_node(&thread_list, client);
#endif
    }
    pthread_mutex_unlock(&thread_list_mutex);

    close(server_socket_fd);
#if    (!USE_AESD_CHAR_DEVICE)
    close(data_packet_fd);
#endif  //(!USE_AESD_CHAR_DEVICE)

#if (!USE_AESD_CHAR_DEVICE)
    unlink(file_path);
#endif /*(!USE_AESD_CHAR_DEVICE)*/
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
#if  (!USE_AESD_CHAR_DEVICE)
    // Start the timestamp logging 
    start_timer();
#endif /*(!USE_AESD_CHAR_DEVICE)*/
    // Run the server
    run_server(PORT, FILE_PATH);

    // Clean up
    closelog();
    return 0;
}