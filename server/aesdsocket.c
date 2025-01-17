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
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>
#include <sys/time.h>

#define BUF_SIZE 256

typedef struct _threadHandle_t
{
    int fd;
    pthread_t thread;
} threadHandle_t;

typedef struct _threadList_t
{
    threadHandle_t *thread;
    SLIST_ENTRY(_threadList_t) entries;
} threadList_t;

const char*             socket_file = "/var/tmp/aesdsocketdata";
int                     sock_fd;
struct addrinfo         *provider;
static pthread_mutex_t  mutex;
static SLIST_HEAD(slisthead, _threadList_t) head = SLIST_HEAD_INITIALIZER(head);
static volatile sig_atomic_t exit_flag = 0;

// Function Prototypes
void daemon(void);
void signal_handler(int sig);
int  receive_data(int fd);
int  send_data(int fd);
void cleanup(void);
void* handle_connection(void* arg);
void* timestamp_thread(void* arg);

int main(int argc, char **argv)
{
    (void) argc; (void) argv;

    struct sigaction    sa                      = {0};
    struct addrinfo     netif                   = {0};
    struct sockaddr_in  *ipv4                   = NULL;
    char                ipstr[INET_ADDRSTRLEN]  = {0};
    int                 sock_in_fd              = 0;
    pthread_t           ts_thread;

    // Setting up the syslog
    openlog(NULL,0,LOG_USER);

    // Creating mutex
    if (pthread_mutex_init(&mutex, NULL) != 0)
    {
        syslog(LOG_ERR, "**ERROR pthread_mutex_init: %s\n", strerror(errno));
        exit(-1);
    }

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

    // Creating socket
    sock_fd = socket(provider->ai_family, provider->ai_socktype, provider->ai_protocol);
    if (sock_fd == -1)
    {
        syslog(LOG_ERR, "**ERROR socket: %s\n", strerror(errno));
        exit(-1);
    }

    // Binding socket
    if (bind(sock_fd, provider->ai_addr, provider->ai_addrlen) == -1)
    {
        syslog(LOG_ERR, "**ERROR bind: %s\n", strerror(errno));
        exit(-1);
    }

    // Listening on socket
    if (listen(sock_fd, 10) == -1)
    {
        syslog(LOG_ERR, "**ERROR listen: %s\n", strerror(errno));
        exit(-1);
    }

    // Creating timestamp thread
    if (pthread_create(&ts_thread, NULL, timestamp_thread, NULL) != 0)
    {
        syslog(LOG_ERR, "**ERROR pthread_create: %s\n", strerror(errno));
        exit(-1);
    }

    while (!exit_flag)
    {
        sock_in_fd = accept(sock_fd, NULL, NULL);
        if (sock_in_fd == -1)
        {
            if (exit_flag) break;
            syslog(LOG_ERR, "**ERROR accept: %s\n", strerror(errno));
            continue;
        }

        threadHandle_t *thread_handle = malloc(sizeof(threadHandle_t));
        if (!thread_handle)
        {
            syslog(LOG_ERR, "**ERROR malloc: %s\n", strerror(errno));
            close(sock_in_fd);
            continue;
        }

        thread_handle->fd = sock_in_fd;
        if (pthread_create(&thread_handle->thread, NULL, handle_connection, thread_handle) != 0)
        {
            syslog(LOG_ERR, "**ERROR pthread_create: %s\n", strerror(errno));
            close(sock_in_fd);
            free(thread_handle);
            continue;
        }

        threadList_t *thread_list = malloc(sizeof(threadList_t));
        if (!thread_list)
        {
            syslog(LOG_ERR, "**ERROR malloc: %s\n", strerror(errno));
            close(sock_in_fd);
            free(thread_handle);
            continue;
        }

        thread_list->thread = thread_handle;
        SLIST_INSERT_HEAD(&head, thread_list, entries);
    }

    // Cleanup
    cleanup();
    return 0;
}

void* handle_connection(void* arg)
{
    threadHandle_t *thread_handle = (threadHandle_t*)arg;
    int fd = thread_handle->fd;

    while (1)
    {
        if (receive_data(fd) <= 0) break;
        if (send_data(fd) <= 0) break;
    }

    close(fd);
    free(thread_handle);
    pthread_exit(NULL);
}

void* timestamp_thread(void* arg)
{
    (void)arg;
    while (!exit_flag)
    {
        sleep(10);
        pthread_mutex_lock(&mutex);
        FILE *file = fopen(socket_file, "a");
        if (file)
        {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char buffer[64];
            strftime(buffer, sizeof(buffer), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);
            fputs(buffer, file);
            fclose(file);
        }
        pthread_mutex_unlock(&mutex);
    }
    pthread_exit(NULL);
}

void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        exit_flag = 1;
        close(sock_fd);
    }
}

void cleanup(void)
{
    threadList_t *thread_list;
    while (!SLIST_EMPTY(&head))
    {
        thread_list = SLIST_FIRST(&head);
        pthread_join(thread_list->thread->thread, NULL);
        SLIST_REMOVE_HEAD(&head, entries);
        free(thread_list->thread);
        free(thread_list);
    }
    pthread_mutex_destroy(&mutex);
    closelog();
}

// Implement receive_data and send_data functions
int receive_data(int fd)
{
    unsigned char buf[BUF_SIZE] = {0};
    int outfile_fd = open(socket_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (outfile_fd < 0) {
        syslog(LOG_ERR, "**ERROR open: %s\n", strerror(errno));
        close(outfile_fd);
        cleanup();
        return -1;
    }

    ssize_t recv_bytes;
    while ((recv_bytes = recv(fd, buf, sizeof(buf), 0)) > 0)
    {
        write(outfile_fd, buf, recv_bytes);
        if (buf[recv_bytes - 1] == '\n')
            break;
    }
    close(outfile_fd);

    return 0;
}

int send_data(int fd)
{
    unsigned char buf[BUF_SIZE] = {0};
    int outfile_fd = open(socket_file, O_RDONLY);

    if (outfile_fd < 0) {
        syslog(LOG_ERR, "**ERROR open: %s\n", strerror(errno));
        close(outfile_fd);
        return -1;
    }

    ssize_t recv_bytes;
    while ((recv_bytes = read(outfile_fd, buf, sizeof(buf))) > 0)
    {
        if ((send(fd, buf, recv_bytes, 0)) < 0) {
            syslog(LOG_ERR, "**ERROR send: %s\n", strerror(errno));
            close(outfile_fd);
            return -1;
        }
    }

    return 0;
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
        exit(0);
    }
}