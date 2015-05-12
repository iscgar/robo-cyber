#ifdef SERVER

#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "proto.h"
#include "frag.h"

#define CONTROLLER_IP_INDEX 1
#define ROBOT_IP_INDEX 2

#if defined(RPI)
#   define CONTROLLER_SEND_PORT_INDEX 3
#   define CONTROLLER_RECV_PORT_INDEX 3
#   define ROBOT_SEND_PORT_INDEX 4
#   define ROBOT_RECV_PORT_INDEX 4
#else
#   define CONTROLLER_SEND_PORT_INDEX 3
#   define CONTROLLER_RECV_PORT_INDEX 4
#   define ROBOT_SEND_PORT_INDEX 5
#   define ROBOT_RECV_PORT_INDEX 6
#endif

int start_app(int argc, const char *argv[])
{
    if (argc != SERVER_ARGUMENTS)
    {
        printf("Usage:\r\n"
               "  %s <Remote Control IP> <Robot IP>"
#if defined(RPI)
               " <Remote Control Port> <Robot Port>\n"
#else
               " <Remote Control Port SEND> <Remote Control Port RECV>"
               " <Robot Port SEND> <Robot Port RECV>\n"
#endif
               , argv[0]);

        return EXIT_FAILURE;
    }

    StartTransferLoop(argv[CONTROLLER_IP_INDEX],
                      argv[ROBOT_IP_INDEX],
                      argv[CONTROLLER_SEND_PORT_INDEX],
                      argv[CONTROLLER_RECV_PORT_INDEX],
                      argv[ROBOT_SEND_PORT_INDEX],
                      argv[ROBOT_RECV_PORT_INDEX]);

    return EXIT_SUCCESS;
}

#endif