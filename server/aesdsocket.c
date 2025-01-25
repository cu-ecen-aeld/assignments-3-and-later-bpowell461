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
#include <time.h>

// Macros
#define BUF_SIZE 4096

// Type Definitions
typedef struct _thread_data_t {
    pthread_t thread_id;
    int thread_fd;
    char ipstr[INET_ADDRSTRLEN];
    bool exit_thread;
} thread_data_t;

typedef enum {
    STATE_RECEIVE = 0,
    STATE_SEND,
    STATE_EXIT,
    STATE_COUNT
}thread_state_t;

struct queue_t {
    thread_data_t data;
    SLIST_ENTRY(queue_t) entries;
};

// Static/Global Variables
static SLIST_HEAD(queue_head, queue_t) head = SLIST_HEAD_INITIALIZER(head);
static int sock_fd;
static struct addrinfo *provider;
const char* socket_file = "/var/tmp/aesdsocketdata";
static sig_atomic_t exitProgram = false;
static int outfile_fd;

pthread_mutex_t fileMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threadMtx = PTHREAD_MUTEX_INITIALIZER;

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
    syslog(LOG_INFO, "Starting socket server\n");

    SLIST_INIT(&head);

    sa.sa_handler = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Registering signals
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    // Setting up the network interface
    memset(&netif, 0, sizeof(netif));
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
    if (listen(sock_fd, 20) == -1) {
        syslog(LOG_ERR,"**ERROR listen: %s\n", strerror(errno));
        exit(-1);
    }

    outfile_fd = open(socket_file, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (outfile_fd < 0) {
        syslog(LOG_ERR, "**ERROR open: %s\n", strerror(errno));
        close(outfile_fd);
        return -1;
    }

    // Creating a thread to handle the connection
    struct queue_t *timeElm = malloc(sizeof(struct queue_t));
    memset(timeElm, 0, sizeof(struct queue_t));
    timeElm->data.exit_thread = false;

    pthread_create(&timeElm->data.thread_id, NULL, timerFunction, &timeElm->data);

    pthread_mutex_lock(&threadMtx);
    SLIST_INSERT_HEAD(&head, timeElm, entries);
    pthread_mutex_unlock(&threadMtx);
    while (!exitProgram)
    {   
        // Accepting the connection
        int sock_in_fd = accept(sock_fd, provider->ai_addr, &provider->ai_addrlen);
        if (sock_in_fd == -1) {
            syslog(LOG_ERR,"**ERROR accept: %s\n", strerror(errno));
            exit(-1);
        }

        // Creating a thread to handle the connection
        struct queue_t *elm = malloc(sizeof(struct queue_t));
        memset(elm, 0, sizeof(struct queue_t));
        elm->data.thread_fd = sock_in_fd;

        struct sockaddr_in *ipv4 = (struct sockaddr_in *)provider->ai_addr;
        inet_ntop(provider->ai_family, &(ipv4->sin_addr), elm->data.ipstr, sizeof elm->data.ipstr);

        if (pthread_create(&elm->data.thread_id, NULL, threadFunction, &elm->data) != 0)
        {
            syslog(LOG_ERR, "**ERROR pthread_create: %s\n", strerror(errno));
            free(elm);
            close(sock_in_fd);
            continue;
        }

        pthread_mutex_lock(&threadMtx);
        SLIST_INSERT_HEAD(&head, elm, entries);
        pthread_mutex_unlock(&threadMtx);
    }

    cleanup();

    return 0;
}

void *threadFunction(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    int fd = data->thread_fd;
    thread_state_t state = STATE_RECEIVE;

    syslog(LOG_INFO, "Accepted connection from %s\n", data->ipstr);

    while(!data->exit_thread)
    {
        if (exitProgram)
        {
            state = STATE_EXIT;
            break;
        }

        switch(state)
        {
            case STATE_RECEIVE:
                state = receive_data(fd) == 0 ? STATE_SEND : STATE_EXIT;
                break;
            case STATE_SEND:
                // For now lets just exit the thread after sending the data
                state = send_data(fd) == 0 ? STATE_EXIT : STATE_EXIT;
                break;
            case STATE_EXIT:
                data->exit_thread = true;
                break;
            default:
                break;  
        }
    }

    syslog(LOG_INFO, "Closed connection from %s\n", data->ipstr);
    close(fd);
    pthread_exit(NULL);
}

void *timerFunction(void *arg)
{
    (void) arg;
    while (!exitProgram) 
    {
        sleep(10);
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[100];
        strftime(timestamp, BUF_SIZE, "timestamp: %a, %d %b %Y %T %z\n", tm_info);
        pthread_mutex_lock(&fileMtx);
        write(outfile_fd, timestamp, strlen(timestamp));
        pthread_mutex_unlock(&fileMtx);
    }

    return NULL;
}

int receive_data(int fd)
{
    char* buf = calloc(BUF_SIZE, sizeof(char));

    ssize_t recv_bytes;
    ssize_t total_bytes = 0;
    while ((recv_bytes = recv(fd, buf + total_bytes, BUF_SIZE - total_bytes - 1, 0)) > 0)
    {
        total_bytes += recv_bytes;
        buf[total_bytes] = '\0';
        if (strchr(buf, '\n') != NULL)
            break;
    }
    pthread_mutex_lock(&fileMtx);
    write(outfile_fd, buf, total_bytes);
    fdatasync(outfile_fd);
    pthread_mutex_unlock(&fileMtx);

    free(buf);

    return 0;
}

int send_data(int fd)
{
    char* buf = malloc(BUF_SIZE);

    ssize_t recv_bytes;

    lseek(outfile_fd, 0, SEEK_SET);
    pthread_mutex_lock(&fileMtx);
    while ((recv_bytes = read(outfile_fd, buf, sizeof(buf)-1)) > 0)
    {
        buf[recv_bytes] = '\0';
        if ((send(fd, buf, recv_bytes, 0)) == -1) {
            syslog(LOG_ERR, "**ERROR send: %s\n", strerror(errno));
            break;
        }
    }
    pthread_mutex_unlock(&fileMtx);
    free(buf);
    return 0;
}

void signal_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
    {
        syslog(LOG_INFO, "Caught signal, exiting\n");
    }
    exitProgram = true;
}

void daemon(void)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        syslog(LOG_ERR, "**ERROR fork: %s\n", strerror(errno));
        exit(-1);
    }
    else if (pid > 0)
    {
        // Parent process
        syslog(LOG_INFO, "Daemon process started with PID %d\n", pid);
        exit(0);
    }
    else
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

        close(fd);
    }
}

void cleanup(void)
{
    pthread_mutex_lock(&threadMtx);
    while (!SLIST_EMPTY(&head))
    {
        struct queue_t *entry = SLIST_FIRST(&head);
        entry->data.exit_thread = true;
        pthread_join(entry->data.thread_id, NULL);
        
        SLIST_REMOVE_HEAD(&head, entries);

        if (entry != NULL)
            free(entry);

        entry = NULL;
    }
    pthread_mutex_unlock(&threadMtx);

    if (provider != NULL)
        freeaddrinfo(provider);

    remove(socket_file);
    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    close(outfile_fd);
    pthread_mutex_destroy(&fileMtx);
    pthread_mutex_destroy(&threadMtx);
    closelog();
}