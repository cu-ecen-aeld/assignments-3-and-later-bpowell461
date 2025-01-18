#define _XOPEN_SOURCE 700
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <syslog.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/signal.h>
#include <sys/queue.h>
#include <pthread.h>

// Macros
#define BUF_SIZE 256

// Type Definitions
typedef struct _thread_data_t {
    pthread_t thread_id;
    int thread_fd;
    struct addrinfo *provider;
    bool exit_thread;
} thread_data_t;

struct queue_t {
    thread_data_t *data;
    SLIST_ENTRY(queue_t) entries;
};

// Static/Global Variables
static SLIST_HEAD(queue_head, queue_t) head = SLIST_HEAD_INITIALIZER(head);
static int sock_fd;
static struct addrinfo *provider;
const char* socket_file = "/var/tmp/aesdsocketdata";
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Function Prototypes
void daemon(void);
void signal_handler(int sig);
int  receive_data(int fd);
int  send_data(int fd);
void cleanup(void);
void *threadFunction(void *arg);
void *timerFunction(void *arg);

int main(int argc, char **argv)
{
    (void) argc; (void) argv;

    struct sigaction    sa                      = {0};
    struct addrinfo     netif                   = {0};

    // Setting up the syslog
    openlog(NULL,0,LOG_USER);

    SLIST_INIT(&head);

    sa.sa_handler = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    // Registering signals
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Setting up the network interface
    netif.ai_family = AF_INET;
    netif.ai_socktype = SOCK_STREAM;
    netif.ai_flags = AI_PASSIVE;

    // Getting the address info
    if (getaddrinfo(NULL, "9000", &netif, &provider) != 0)
    {
        syslog(LOG_ERR, "**ERROR getaddrinfo: %s\n", strerror(errno));
        exit(-1);
    }

    // Creating the socket and binding
    sock_fd = socket(provider->ai_family, provider->ai_socktype, provider->ai_protocol);
    if (sock_fd == -1) {
        syslog(LOG_ERR,"**ERROR socket: %s\n", strerror(errno));
        exit(-1);
    }

    int flags = 1;
    if (setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR, &flags, sizeof(flags)) == -1) {
        syslog(LOG_ERR, "**ERROR setsockopt: %s\n", strerror(errno));
        exit(-1);
    }

    if (bind(sock_fd, provider->ai_addr, provider->ai_addrlen) == -1) {
        syslog(LOG_ERR,"**ERROR bind: %s\n", strerror(errno));
        exit(-1);
    }

    // Fork the process to run as a daemon
    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        daemon();
    }

    // Listening on the socket
    if (listen(sock_fd, 10) == -1) {
        syslog(LOG_ERR,"**ERROR listen: %s\n", strerror(errno));
        exit(-1);
    }

    // Creating a thread to handle the connection
    struct queue_t *timeElm = malloc(sizeof(struct queue_t));
    thread_data_t *timeData = malloc(sizeof(thread_data_t));
    timeData->exit_thread = false;

    pthread_create(&timeData->thread_id, NULL, timerFunction, timeData);

    SLIST_INSERT_HEAD(&head, timeElm, entries);

    while (1)
    {   
        // Accepting the connection
        int sock_in_fd = accept(sock_fd, provider->ai_addr, &provider->ai_addrlen);
        if (sock_in_fd == -1) {
            syslog(LOG_ERR,"**ERROR accept: %s\n", strerror(errno));
            exit(-1);
        }

        // Creating a thread to handle the connection
        struct queue_t *elm = malloc(sizeof(struct queue_t));
        thread_data_t *data = malloc(sizeof(thread_data_t));
        data->thread_fd = sock_in_fd;
        data->provider = provider;

        pthread_create(&data->thread_id, NULL, threadFunction, data);

        SLIST_INSERT_HEAD(&head, elm, entries);
    }

    return 0;
}

void *threadFunction(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    int fd = data->thread_fd;
    struct addrinfo *provider = data->provider;
    char ipstr[INET_ADDRSTRLEN];
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)provider->ai_addr;
    inet_ntop(provider->ai_family, &(ipv4->sin_addr), ipstr, sizeof ipstr);

    syslog(LOG_INFO, "Accepted connection from %s\n", ipstr);

    pthread_mutex_lock(&mutex);

    if (receive_data(fd) != 0)
    {
        data->exit_thread = true;
    }

    pthread_mutex_unlock(&mutex);

    if (send_data(fd) != 0)
    {
        data->exit_thread = true;
    }

    syslog(LOG_INFO, "Closed connection from %s\n", ipstr);
    close(fd);
    return NULL;
}

void *timerFunction(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    time_t last_print = time(NULL);
    while (!data->exit_thread) 
    {
        time_t now = time(NULL);

        if (!(difftime(now, last_print) >= 10.0))
        {
            continue;
        }

        pthread_mutex_lock(&mutex);

        now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[BUF_SIZE];
        strftime(timestamp, BUF_SIZE, "timestamp: %a, %d %b %Y %T %z\n", tm_info);

        int outfile_fd = open(socket_file, O_CREAT | O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO, 0644);
        if (outfile_fd < 0) {
            syslog(LOG_ERR, "**ERROR open: %s\n", strerror(errno));
            pthread_mutex_unlock(&mutex);
            continue;
        }

        write(outfile_fd, timestamp, strlen(timestamp));
        close(outfile_fd);

        last_print = now;

        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

int receive_data(int fd)
{
    unsigned char buf[BUF_SIZE] = {0};
    int outfile_fd = open(socket_file, O_CREAT | O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO, 0644);
    if (outfile_fd < 0) {
        syslog(LOG_ERR, "**ERROR open: %s\n", strerror(errno));
        close(outfile_fd);
        return -1;
    }

    ssize_t recv_bytes;
    while ((recv_bytes = recv(fd, buf, sizeof(buf)-1, 0)) > 0)
    {
        write(outfile_fd, buf, recv_bytes);
        if (buf[recv_bytes - 1] == '\n')
            break;
    }
    lseek(outfile_fd, 0, SEEK_SET);
    fdatasync(outfile_fd);
    close(outfile_fd);

    return 0;
}

int send_data(int fd)
{
    unsigned char buf[BUF_SIZE] = {0};
    int outfile_fd = open(socket_file, O_CREAT | O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);

    if (outfile_fd < 0) {
        syslog(LOG_ERR, "**ERROR open: %s\n", strerror(errno));
        close(outfile_fd);
        return -1;
    }

    ssize_t recv_bytes;
    while ((recv_bytes = read(outfile_fd, buf, sizeof(buf)-1)) > 0)
    {
        if ((send(fd, buf, recv_bytes, 0)) != recv_bytes) {
            syslog(LOG_ERR, "**ERROR send: %s\n", strerror(errno));
            close(outfile_fd);
            return -1;
        }
    }

    return 0;
}

void signal_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
    {
        syslog(LOG_INFO, "Caught signal, exiting\n");
        cleanup();
        if (remove(socket_file) != 0)
            syslog(LOG_ERR, "**ERROR remove: %s\n", strerror(errno));
        exit(0);
    }
}

void daemon(void)
{
    pid_t pid = fork();
    if (pid == -1)
    {
        syslog(LOG_ERR, "**ERROR fork: %s\n", strerror(errno));
        exit(-1);
    }

    if (pid == 0)
    {
        // Child process
        setsid();
        chdir("/");
        int fd = open("/dev/null", O_RDWR);
        if (fd < 0) 
        {
            syslog(LOG_ERR, "**ERROR open: %s\n", strerror(errno));
            exit(-1);
        }

        if(dup2(fd, STDIN_FILENO) == -1)
            syslog(LOG_ERR, "error while redirection STDIN to /dev/null: %s\n", strerror(errno));
        if(dup2(fd, STDOUT_FILENO) == -1)
            syslog(LOG_ERR, "error while redirection STDOUT to /dev/null: %s\n", strerror(errno));
        if(dup2(fd, STDERR_FILENO) == -1)
            syslog(LOG_ERR, "error while redirection STDERR to /dev/null: %s\n", strerror(errno));
    }
    else
    {
        // Parent process
        syslog(LOG_INFO, "Daemon process started with PID %d\n", pid);
        exit(0);
    }
}

void cleanup(void)
{
    while (!SLIST_EMPTY(&head))
    {
        struct queue_t *entry = SLIST_FIRST(&head);
        entry->data->exit_thread = true;
        pthread_join(entry->data->thread_id, NULL);
        
        SLIST_REMOVE_HEAD(&head, entries);

        if (entry->data != NULL)
            free(entry->data);

        if (entry != NULL)
            free(entry);
    }
    close(sock_fd);
    closelog();
}