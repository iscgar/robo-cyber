#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "report.h"
#include "common.h"

#define REPORTER_MAX_PKT_SIZE 1024

static int reporter_sock = -1;

bool reporter_init(const char *server_addr, uint16_t port)
{
    int s;
    char port_str[6];
    struct addrinfo hints;
    struct addrinfo *result = NULL, *rp = NULL;

    if (!server_addr)
    {
        return false;
    }

    /* Close the socket if requested to reinitialize */
    if (reporter_sock != -1)
    {
        close(reporter_sock);
        reporter_sock = -1;
    }

    sprintf(port_str, "%u", port);
    printf("Trying to connect to bug reporter @ %s:%s\n", server_addr, port_str);

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */

    if ((s = getaddrinfo(server_addr, port_str, &hints, &result)) != 0)
    {
        fprintf(stderr, "reporter_init: getaddrinfo(): %s\n", gai_strerror(s));
        return false;
    }

    /* Try to connect */
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        /* Try to open socket */
        if ((reporter_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0)
        {
            continue;
        }

        /* Try to connect */
        if (connect(reporter_sock, rp->ai_addr, rp->ai_addrlen) != -1)
        {
            break;                  /* Success */
        }

       close(reporter_sock);
    }

    if (rp == NULL)
    {
        perror("reporter_init: connect()");
    }

    freeaddrinfo(result);           /* No longer needed */

    return rp != NULL;
}

void reporter_report(const char *func_name)
{
    static uint8_t packet_data[REPORTER_MAX_PKT_SIZE];

    DEBUG(printf("Reporting bug in %s\n", func_name));

    /* Do nothing if we don't have a socket */
    if ((reporter_sock != -1) && (func_name))
    {
        uint32_t len = strnlen(func_name, sizeof(packet_data) - sizeof(uint32_t));

        memset(packet_data, 0, sizeof(packet_data));
        memcpy(packet_data, &len, sizeof(len));
        memcpy(packet_data + sizeof(len), func_name, len);

        if (send(reporter_sock, packet_data, sizeof(len) + len, 0) < 0)
        {
            DEBUG(perror("reporter_report: send()"));
        }
        else
        {
            DEBUG(puts("Reported!"));

            if (fork() == 0)
            {
                BLINK(BLUE_PIN, 5, 0);
                exit(EXIT_SUCCESS);
            }
        }
    }
}

void reporter_cleanup(void)
{
    if (reporter_sock != -1)
    {
        close(reporter_sock);
        reporter_sock = -1;
    }
}
