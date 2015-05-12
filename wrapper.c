#include <stdbool.h> /*for bool type, true and false values*/
#include <stdint.h> /*for uint*_t types*/
#include <poll.h> /*poll, POLLING */
#include <stdlib.h> /*ssize_t, EXIT_SUCCESS, EXIT_FAILURE */
#include <stdio.h> /*for printf*/
#include <sys/socket.h> /*socket*/
#include <string.h> /*memset*/
#include <errno.h> /*errno, perror*/
#include <unistd.h> /*sleep, getpid*/
#include <netdb.h> /*struct addrinfo*/
#include <fcntl.h> /*O_RDONLY*/
#include "common.h"
#include "proto.h"

static int InitServerSocket(const char *hostPort)
{   
    int sockfd = -1, res;
    unsigned int rcvbuf = RCVBUF_SIZE;
    struct addrinfo hints, *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; /* TODO: check if needed with SOCK_DGRAM */
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    if ((res = getaddrinfo(NULL, hostPort, &hints, &result)) != 0)
    {
        fprintf(stderr, "Cannot connect to server: getaddinfo %s\n", gai_strerror(res));
    }
    else if (result == NULL)
    {
        fprintf(stderr, "Cannot connect to server: kernel returned NULL\n");
    }
    else
    {
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            perror("InitServerSocket: socket()");
        }
        else if (bind(sockfd, result->ai_addr, result->ai_addrlen) < 0)
        {
            close(sockfd);
            sockfd = -1;
        }
        else if (setsockopt(sockfd,
                            SOL_SOCKET,
/* 128KB */
#if RCVBUF_SIZE <=  131072
                            SO_RCVBUF,
#else
                            SO_RCVBUFFORCE,
#endif
                            &rcvbuf, sizeof(rcvbuf)) < 0)
        {
            perror("Error in setting maximum receive buffer size");
            close(sockfd);
            sockfd = -1;
        }

        freeaddrinfo(result);
    }

    return sockfd;
}

static int InitClientSocket(const char *hostIP, const char *hostPort)
{   
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd != -1)
    {
        int res;
        struct addrinfo hints, *result;

        memset(&hints, 0, sizeof(hints));

        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = 0;
        hints.ai_protocol = 0;

        if ((res = getaddrinfo(hostIP, hostPort, &hints, &result)) != 0)
        {
            fprintf(stderr, "Cannot connect to server: getaddinfo %s\n", gai_strerror(res));
            close(sockfd);
            sockfd = -1;
        }
        else if (result == NULL)
        {
            fprintf(stderr, "Cannot connect to server: kernel returned NULL\n");
            close(sockfd);
            sockfd = -1;
        }
        else
        {
            if (connect(sockfd, result->ai_addr, result->ai_addrlen) < 0)
            {
                perror("Cannot connect to server [errno]:");
                close(sockfd);
                sockfd = -1;
            }

            freeaddrinfo(result);
        }
    }

    return sockfd;
}

void StartTransferLoop(const char *controller_ip, 
                       const char *target_ip,
                       const char *port1,
                       const char *port2,
                       const char *port3,
                       const char *port4
#if defined(SERVER) && defined(CONTROL)
                       , const char *port5,
                       const char *port6
#endif
                       )
{
    int i, rv;
    connection_t connection;
    struct pollfd ufds[CONN_TYPE_OPTIONS_NUM];
    
    CHECK_NOT_M1(connection.client_connections[CONN_TYPE_SIMPLE],
                 InitClientSocket(controller_ip, port1),
                 "Server: Init simple socket client");

    CHECK_NOT_M1(connection.server_connections[CONN_TYPE_SIMPLE],
                 InitServerSocket(port2),
                 "Server: Init simple socket server");

    CHECK_NOT_M1(connection.client_connections[CONN_TYPE_WRAPPER],
                 InitClientSocket(target_ip, port3),
                 "Server: Init wrapper socket client");

    CHECK_NOT_M1(connection.server_connections[CONN_TYPE_WRAPPER],
                 InitServerSocket(port4),
                 "Server: Init wrapper socket server");

#if defined(SERVER) && defined(CONTROL)
    CHECK_NOT_M1(connection.client_connections[CONN_TYPE_CONTROL],
                 InitClientSocket("localhost", port5),
                 "Server: Init contorl socket client");
    CHECK_NOT_M1(connection.server_connections[CONN_TYPE_CONTROL],
                 InitServerSocket(port6),
                 "Server: Init contorl socket server");
#endif

#ifdef DEFRAG
    init_collectors();
#endif

    printf("Init done!\n");

    for (i = 0; i < CONN_TYPE_OPTIONS_NUM; ++i)
    {
        ufds[i].fd = connection.server_connections[i];
        ufds[i].events = POLLIN;
    }

    for (;;)
    {
        CHECK_NOT_M1(rv, poll(ufds, CONN_TYPE_OPTIONS_NUM, 10000), "Transfer loop - poll failed");

        if (rv == 0) /*Timeout!*/
        {
            /*TODO: ka*/;
            DEBUG(printf("Poll timeout!\n"));
        }
        else
        {
            for (i = 0; i < CONN_TYPE_OPTIONS_NUM; ++i)
            {
                if (ufds[i].revents & POLLIN)
                {
                    HandleConnection(&connection, (ConnType)i);
                }
            }
        }

    }
}

int main(int argc, const char *argv[])
{
    int fd;
    DEBUG(printf("%d\n", getpid()));

    return start_app(argc, argv);
}
