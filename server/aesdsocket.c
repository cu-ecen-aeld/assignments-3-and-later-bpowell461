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

#define BUF_SIZE 256

const char*         socket_file = "/var/tmp/aesdsocketdata";
int                 sock_fd;
struct addrinfo     *provider;

// Function Prototypes
void daemon(void);
void signal_handler(int sig);
int  receive_data(int fd);
int  send_data(int fd);
void cleanup(void);

int main(int argc, char **argv)
{
    (void) argc; (void) argv;

    struct sigaction    sa                      = {0};
    struct addrinfo     netif                   = {0};
    struct sockaddr_in  *ipv4                   = NULL;
    char                ipstr[INET_ADDRSTRLEN]  = {0};
    int                 sock_in_fd              = 0;

    // Setting up the syslog
    openlog(NULL,0,LOG_USER);

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
    ipv4 = (struct sockaddr_in *)provider->ai_addr;
    inet_ntop(provider->ai_family, &(ipv4->sin_addr), ipstr, sizeof ipstr);
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
    if (listen(sock_fd, 5) == -1) {
        syslog(LOG_ERR,"**ERROR listen: %s\n", strerror(errno));
        exit(-1);
    }

    while (1)
    {   
        // Accepting the connection
        sock_in_fd = accept(sock_fd, provider->ai_addr, &provider->ai_addrlen);
        if (sock_in_fd == -1) {
            syslog(LOG_ERR,"**ERROR accept: %s\n", strerror(errno));
            exit(-1);
        }

        ipv4 = (struct sockaddr_in *)provider->ai_addr;
        inet_ntop(provider->ai_family, &(ipv4->sin_addr), ipstr, sizeof ipstr);
        syslog(LOG_ERR,"Accepted connection from %s\n", ipstr);
        
        // Everything went well, let's create a file to store the data
        if (receive_data(sock_in_fd) != 0)
        {
            cleanup();
            exit(-1);
        }

        // Send the data back to the client
        if (send_data(sock_in_fd) != 0)
        {
            cleanup();
            exit(-1);
        }

        syslog(LOG_INFO, "Closed connection from %s\n", ipstr);
    }

    return 0;
}

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

void signal_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
    {
        syslog(LOG_INFO, "Caught signal, exiting\n");
        close(sock_fd);
        freeaddrinfo(provider);
        if (remove(socket_file) != 0)
            syslog(LOG_ERR, "**ERROR remove: %s\n", strerror(errno));

        cleanup();
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

void cleanup(void)
{
    close(sock_fd);
    freeaddrinfo(provider);
    closelog();
}