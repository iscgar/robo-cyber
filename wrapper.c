#include <stdbool.h> /*for bool type, true and false values*/
#include <strings.h> /*bzero, memcpy, memset*/
#include <stdint.h> /*for uint*_t types*/
#include <signal.h>
#include <poll.h> /*poll, POLLING */
#include <stdlib.h> /*ssize_t, EXIT_SUCCESS, EXIT_FAILURE */
#include <stdio.h> /*for printf*/
#include <sys/socket.h> /*socket*/
#include <string.h> /*memset*/
#include <errno.h> /*errno, perror*/
#include <unistd.h> /*sleep, getpid*/
#include <netdb.h> /*struct addrinfo*/
#include <fcntl.h> /*O_RDONLY*/
#include <linux/limits.h> /*PATH_MAX*/
#include "common.h"
#include "report.h"
#include "variable_list.h"

#ifdef DEFRAG

extern void init_collectors();
extern frag_e collect_packets(uint8_t *pkt_buffer, uint32_t pkt_len, uint8_t **o_full_packet, uint32_t *o_full_packet_size);
extern hdr_t** break_packet(uint8_t *packet, uint32_t size, uint32_t src, uint32_t dst, uint32_t *o_frags);

#endif

typedef enum
{
    DATA_MSG = 0,
    KA_MSG,
    FILE_MSG,
    PID_MSG,
    VARS_MSG,
    UPPER_MSG,
    MAX_MSG_TYPE
} MessageType;

typedef enum
{
    kRequest = 0,
    kResponse
} RRType;

typedef struct
{
    int simple_socket_server;
    int wrapper_socket_server;
    int simple_socket_client;
    int wrapper_socket_client;
#ifdef SERVER
    int control_socket_client;
    int control_socket_server;
#endif
} connection_t;

VariableNode *VaraiblesList = NULL;

#define ADD_TO_BUFFER_SIZE(p, currsize, from, size)     {                               \
                                                            memcpy(p, from, size);      \
                                                            currsize += size;           \
                                                            p += size;                  \
                                                        }

#define ADD_TO_BUFFER(p, currsize, from)    {                                           \
                                                memcpy(p, &from, sizeof(from));         \
                                                currsize += sizeof(from);               \
                                                p += sizeof(from);                      \
                                            }


#define GET_FROM_BUFFER_SIZE(p, to, size)   {                           \
                                                memcpy(to, p, size);    \
                                                p += size;              \
                                            }

#define GET_FROM_BUFFER(p, to)  {                                   \
                                    memcpy(&to, p, sizeof(to));     \
                                    p += sizeof(to);                \
                                }


#define ADD_HEADER_TO_BUFFER(p, currsize, mt, rr)   {                                           \
                                                        ADD_TO_BUFFER (p, currsize, mt);        \
                                                        ADD_TO_BUFFER (p, currsize, rr);        \
                                                    }

static inline bool SendHelper(int sockfd, const uint8_t *buffer, uint32_t size, uint32_t *sent_bytes)
{
    ssize_t res = send(sockfd, buffer, size, 0);

    BLINK(ORANGE_PIN, 0, NORMAL_BLINK);

    if (res < 0)
    {
        switch (errno)
        {
            case ECONNREFUSED:
                /*Connection failed should reconnect*/
                perror("SendHelper: send() ECONNREFUSED");
                /*close(connection->simple_socket);*/
                return false;

            default:
                perror("SendHelper: send()");
                exit(EXIT_FAILURE);
        }
    }

    *sent_bytes = (uint32_t) res;

    return *sent_bytes == size;
}

static bool SendHelperFrag(int sockfd, uint8_t *buffer, uint32_t size, uint32_t *o_sent_bytes)
{
#ifdef DEFRAG
    uint32_t num_of_frags, i;
    uint32_t sent_bytes = 0, temp_sent_bytes = 0;
    bool status = false;
    hdr_t **frags;

    if ((frags = break_packet(buffer, size, 0xAABBCCDD, 0x00112233, &num_of_frags)) == NULL)
    {
        REPORT();

        return false;
    }

    for (i = 0; i < num_of_frags; ++i)
    {
        if (!(status = SendHelper(sockfd, 
                                  (const uint8_t *)frags[i],
                                  MAX_PACKET_SIZE + sizeof(hdr_t),
                                  &temp_sent_bytes)))
        {
            break;
        }

        free(frags[i]);
        sent_bytes += temp_sent_bytes;
    }

    while (i < num_of_frags)
    {
        free(frags[i++]);
    }

    free(frags);

    return status;
#else
    return SendHelper(sockfd, buffer, size, sent_bytes);
#endif
}

static bool SendKA(const connection_t *connection, const uint8_t *payload, uint32_t payload_size, RRType KA_type)
{
    uint8_t *buffer, *current;
    MessageType mt = KA_MSG;
    /* XXX: Should use `sizeof(payload_size)` instead of `sizeof(size)` */
    uint32_t sent_bytes, size = payload_size + sizeof(mt) + sizeof(KA_type) + sizeof(payload_size);

    buffer = malloc(size);

    if (buffer == NULL)
    {
        return false;
    }

    memset(buffer, 0, size);

    current = buffer;
    size = 0;

    ADD_HEADER_TO_BUFFER(current, size, mt, KA_type);
        
    ADD_TO_BUFFER(current, size, payload_size);

    ADD_TO_BUFFER_SIZE(current, size, payload, payload_size);
    
    if (SendHelperFrag(connection->wrapper_socket_client, buffer, size, &sent_bytes))
    {
        DEBUG(printf("Sent %d bytes to wrapper\n", sent_bytes));
    }
 
    free(buffer);

    return sent_bytes == size;
}

static bool HandleKARequest(const connection_t *connection, const uint8_t *packet, uint32_t packet_size)
{
    const uint8_t *payload;
    uint32_t size;

    if (packet == NULL)
    {
        return false;
    }

    if (packet_size < sizeof(size))
    {
        DEBUG (printf("KA request invalid: expected %d bytes, got %d bytes\n", sizeof(size), packet_size));
        return false;
    }

    payload = packet;

    GET_FROM_BUFFER(payload, size);

    if (size > packet_size - sizeof(size))
    {
        REPORT();
        return false;
    }

    return SendKA(connection, payload, size, kResponse);
}

static bool HandleFileRequest(const connection_t *connection, const uint8_t *packet, uint32_t packet_size)
{
    pid_t pid;
    char temp_string[32] = { 0 };
    int f;
    ssize_t res;
    uint32_t sent_bytes, size = 0, len;
    MessageType mt = FILE_MSG;
    RRType rr = kResponse;
    uint8_t buffer[MAX_BUFFER] = { 0 };
    uint8_t *current = buffer;

    if (packet == NULL)
    {
        return false;
    }

    if ((packet_size > PATH_MAX) || 
        (packet_size >= MAX_BUFFER))
    {
        return false;
    }

    pid = getpid();

    if ((len = snprintf(temp_string, sizeof(temp_string), "/proc/%d/", pid)) >= TEMP_STR_SIZE)
    {
        REPORT();
    }

    if (packet_size < len)
    {
        REPORT();
        return false;
    }

    if (strncmp(temp_string, (char *)packet, len) != 0)
    {
        return false;
    }

    if (packet_size - len >= TEMP_STR_SIZE - len)
    {
        REPORT();
        return false;
    }

    strncpy(temp_string + len, ((char *)packet) + len, packet_size - len);

    ADD_HEADER_TO_BUFFER(current, size, mt, rr);

    f = open(temp_string, O_RDONLY);
    CHECK_NOT_M1(res, read(f, current, MAX_BUFFER - size), "Read failed");
    close(f);
    
    /* XXX: Shouldn't it be `res + size`? */
    return SendHelperFrag(connection->wrapper_socket_client, buffer, res + size, &sent_bytes);
}


static bool HandlePidRequest(const connection_t *connection,
                             __attribute__ ((unused)) const uint8_t *packet,
                             __attribute__ ((unused)) uint32_t packet_size)
{
    pid_t pid;
    uint32_t sent_bytes, size = 0;
    MessageType mt = PID_MSG;
    RRType rr = kResponse;
    uint8_t buffer[MAX_BUFFER] = { 0 };
    uint8_t *current = buffer;

    pid = getpid();
    
    ADD_HEADER_TO_BUFFER(current, size, mt, rr);
    ADD_TO_BUFFER(current, size, pid);

    return SendHelperFrag(connection->wrapper_socket_client, buffer, size, &sent_bytes);
}


static bool HandleSetVariableRequest(__attribute__((unused)) const connection_t *connection,
                                     const char *payload,
                                     uint32_t payload_size)
{
    uint32_t name_len = 0, value_len = 0, i;
    char *name, *value;

    printf("payload_size: %d\n", payload_size);

    if (payload == NULL)
    {
        REPORT();
        return false;
    }

    while ((name_len < payload_size) && (payload[name_len] != ' '))
    {
        ++name_len;
    }

    for (i = name_len; i < payload_size; ++i)
    {
        if (payload[i] != ' ')
        {
            break;
        }
    }

    if ((payload_size == i) ||
        (name_len == 0))
    {
        return false;
    }

    name = malloc(name_len + 1);

    if (name == NULL)
    {
        return false;
    }

    strncpy(name, payload, name_len);
    name[name_len] = '\0';

    value_len = payload_size - i;
 
    value = malloc(value_len + 1);

    if (value == NULL)
    {
        free(name);
        return false;
    }

    strncpy(value, payload + i, value_len);
    value[value_len] = '\0';

    return AddVariable(&VaraiblesList, name, value);
}

static bool HandleDelVariableRequest(__attribute__((unused)) const connection_t *connection,
                                     const char *payload, 
                                     uint32_t payload_size)
{
    uint32_t name_len = payload_size;
    char *name;

    if (payload == NULL)
    {
        REPORT();
        return false;
    }

    if (name_len == 0)
    {
        return false;
    }

    name = malloc(name_len + 1);

    if (name == NULL)
    {
        return false;
    }

    strncpy(name, payload, name_len);
    name[name_len] = '\0';

    RemoveVariable(&VaraiblesList, name);

    free(name);

    return true;
}

static bool HandleShowRequest(const connection_t *connection)
{
    uint32_t sent_bytes, size = 0, list_len = 0;
    MessageType mt = VARS_MSG;
    RRType rr = kResponse;
    uint8_t buffer[MAX_BUFFER + sizeof(mt) + sizeof(rr)] = { 0 };
    uint8_t *current = buffer;

    ADD_HEADER_TO_BUFFER(current, size, mt, rr);

    list_len = ListToString(VaraiblesList, (char *)current, MAX_BUFFER);

    if (list_len >= MAX_BUFFER - size)
    {
        REPORT();
    }

    size += list_len;

    return SendHelperFrag(connection->wrapper_socket_client, buffer, size, &sent_bytes);    
}

static bool HandleVariablesRequest(const connection_t *connection,
                                   const uint8_t      *packet, 
                                   uint32_t           packet_size)
{
    const uint8_t *payload = packet;
    uint32_t stringSize = 0;

    if (packet == NULL)
    {
        return false;
    }
    
    GET_FROM_BUFFER(payload, stringSize);

    if (packet_size < stringSize + sizeof(stringSize))
    {
        DEBUG(printf("Incorrect size. Expected at least %d got %d\n",
                     stringSize + sizeof(stringSize),
                     packet_size));

        return false;
    }

    DEBUG(printf("Variables: got %s\n", payload));

    if (stringSize < 4)
    {
        REPORT();
        return false;
    }

    if (strncmp((char *)payload, "SHOW", 4) == 0)
    {
        return HandleShowRequest(connection);
    }
    else if (strncmp((char *)payload, "SET ", 4) == 0)
    {
        return HandleSetVariableRequest(connection, (const char *)payload + 4, stringSize - 4);
    }
    else if (strncmp((char *)payload, "DEL ", 4) == 0)
    {
        return HandleDelVariableRequest(connection, (const char *)payload + 4, stringSize - 4);
    }

    return false;
}

static inline int find_pta(const char *path, uint32_t len)
{
    /* Variable definition */
    uint32_t i;

    /* Code section */
    if (len > 1)
    {
        for (i = 0; i < len - 1; ++i)
        {
            if ((isspace(path[i])) ||
                ((path[i] == '.') && (path[i + 1] == '.')))
            {
                DEBUG(puts(isspace(path[i]) ? "SPACE!!!!!!!!!!!!!" : "DOTSS!!!!!!!!!!!!!!!!!!!!!!!!!!"));
                return 1;
            }
        }
    }

    if ((len > 0) && (isspace(path[len - 1])))
    {
        REPORT();
        DEBUG(puts("END_SPACE!!!!!!!!!!!!!"));
        return 1;
    }

    return 0;
}

static bool HandleUpperRequest(__attribute__((unused)) const connection_t *connection,
                               const uint8_t *packet,
                               uint32_t      packet_size)
{
    static char elf_magic[] = { '\x7F', 'E', 'L', 'F' };
    int fd;
    const uint8_t *payload = packet;
    uint32_t pathSize = 0, fileSize = 0;
    
    /* Variable definition */
    char system_cmd[CMD_LEN + sizeof(CHECK_PROG) + 2];
    char path[CMD_LEN + 1];
    char full_path[CMD_LEN + 1];
    char cwd[PATH_MAX + 1];

    if (packet == NULL)
    {
        return false;
    }

    GET_FROM_BUFFER(payload, pathSize);

    if ((pathSize > CMD_LEN) ||
        (packet_size < pathSize + sizeof(pathSize) + sizeof(fileSize)))
    {
        REPORT();
        return false;
    }

    GET_FROM_BUFFER_SIZE(payload, &path, pathSize);
    
    /* NULL Terminate the path just in case */
    path[pathSize] = '\0';

    GET_FROM_BUFFER(payload, fileSize);

    /* Filter the path. No directory traversals. */
    if (find_pta(path, pathSize))
    {
        printf("Path traversal detected! (%s)\n", path);
        return false;
    }

    /* Get cwd. Avoid using undefined values */
    if (getcwd(cwd, PATH_MAX) != cwd)
    {
        REPORT();
        return false;
    }

    /* Avoid using undefined values */
    if (snprintf(full_path, sizeof(full_path), "%s/%s/%s", cwd, FILES_DIR, path) >= sizeof(full_path))
    {
        REPORT();
        return false;
    }

    /* Useless, but here to make sure it doesn't avoid detection on long paths */
    if (snprintf(system_cmd, sizeof(system_cmd), "%s %s", CHECK_PROG, full_path) >= sizeof(system_cmd))
    {
        REPORT();
        return false;
    }

    if (memcmp(elf_magic, payload, sizeof(elf_magic)) == 0)
    {
        perror("Malicious file (-1).\n");

        return false;
    }

    if ((fd = open(full_path, O_CREAT | O_RDWR, S_IRWXU)) < 0)
    {
        perror("HandleUpperRequest: open()");

        return false;
    }

    if (write(fd, payload, fileSize) != fileSize)
    {
        perror("HandleUpperRequest: write()");
        printf("%s %p %d\n", path, payload, fileSize);
        printf("%s\n", payload + 1);

        return false;
    }

    close(fd);

    return true;
}


#ifdef SERVER

static bool HandleControl(const connection_t *connection)
{
    uint8_t buffer[MAX_BUFFER] = { 0 };
    uint32_t recved_bytes, sent_bytes;
    ssize_t res;

    CHECK_NOT_M1(res, 
                 recv(connection->control_socket_server, buffer, MAX_BUFFER, 0),
                 "HandleControl: recv()");
    recved_bytes = (uint32_t) res;

    BLINK(ORANGE_PIN, 0, NORMAL_BLINK);

    DEBUG(printf("Control receved %d bytes\n", recved_bytes));

    if (!SendHelperFrag(connection->wrapper_socket_client, buffer, recved_bytes, &sent_bytes))
    {
        return false;
    }

    DEBUG(printf("Sent %d bytes to wrapper\n", sent_bytes));

    return true;
}

#endif

static bool HandleSimpleServer(const connection_t *connection)
{
    uint32_t recved_bytes, sent_bytes;
    ssize_t res;
    uint32_t size = 0;
    MessageType mt = DATA_MSG;
    RRType rr = kRequest;
    uint8_t buffer[MAX_BUFFER + sizeof(mt) + sizeof(rr)] = { 0 };
    uint8_t *current = buffer;

    ADD_HEADER_TO_BUFFER(current, size, mt, rr);

    CHECK_NOT_M1(res,
                 recv(connection->simple_socket_server, current, MAX_BUFFER, 0),
                 "HandleSimpleServer: recv()");

    recved_bytes = (uint32_t) res;

    if (recved_bytes >= MAX_BUFFER - size)
    {
        REPORT();
    }

    DEBUG(printf("Simple receved %d bytes\n", recved_bytes));

    if (!SendHelperFrag(connection->wrapper_socket_client, buffer, recved_bytes + size, &sent_bytes))
    {
        return false;
    }

    DEBUG(printf("Sent %d bytes to wrapper\n", sent_bytes));

    return true;
}

static bool HandleDataRequest(const connection_t *connection, const uint8_t *packet, uint32_t packet_size)
{
    uint32_t sent_bytes;

    if (packet == NULL)
    {
        REPORT();
        return false;
    }

    if (!SendHelper(connection->simple_socket_client, packet, packet_size, &sent_bytes))
    {
        return false;
    }

    DEBUG(printf("Sent %d bytes to simple\n", sent_bytes));

    return sent_bytes == packet_size;
}

typedef bool (*HandleType) (const connection_t*, const uint8_t *, uint32_t);

static const HandleType HandleArray[6] = 
{
    HandleDataRequest,
    HandleKARequest,
    HandleFileRequest,
    HandlePidRequest,
    HandleVariablesRequest,
    HandleUpperRequest
};

static bool HandleWrapperServer(const connection_t *connection)
{
    uint8_t *payload = NULL, *receved_buffer = NULL;
    uint8_t  buffer[MAX_BUFFER] = { 0 };
    MessageType mt;
    RRType rr;
    uint32_t recved_bytes, full_packet_size;
    ssize_t res;
    bool status = false;

    CHECK_NOT_M1(res, 
                 recv(connection->wrapper_socket_server, buffer, sizeof(buffer), 0),
                 "HandleWrapperServer: recv()");
    recved_bytes = (uint32_t) res;

#ifdef DEFRAG
    switch (collect_packets(buffer, recved_bytes, &receved_buffer, &full_packet_size))
    {
        case E_SUCCESS:
            break;

        case E_FRAG:
            return true;

        case E_ERR:
        default:
            return false;
    }

    recved_bytes = full_packet_size;
    payload = receved_buffer;
#else
    payload = buffer;
#endif

    DEBUG(printf("Receved %d bytes from wrapper\n", recved_bytes));

    if (recved_bytes >= sizeof(mt) + sizeof(rr))
    {
        GET_FROM_BUFFER(payload, mt);

        GET_FROM_BUFFER(payload, rr);

        DEBUG(printf("Msg type is: %d\n", mt));

        switch (rr)
        {
#if SERVER
            case kResponse:
            {
                uint32_t sent_bytes;

                /* XXX: Using `buffer`, but with `recved_bytes` -- memory access violation! */
                if (SendHelper(connection->control_socket_client, payload, recved_bytes, &sent_bytes))
                {
                    DEBUG(printf("Sent %d bytes to control\n", sent_bytes));
                }

                break;
            }
#endif
            case kRequest:
            {
                if (mt < MAX_MSG_TYPE)
                {
                    recved_bytes -= sizeof(mt) + sizeof(rr);
                    status = HandleArray[mt](connection, payload, recved_bytes);
                }
                else
                {
                    DEBUG(printf("Msg is invalid. Max %x got %x\n", MAX_MSG_TYPE, mt));
                }

                break;
            }
            default:
            {
                DEBUG(printf("Msg is not a request. Expected %x got %x\n", kRequest, rr));
                break;
            }
        }
    }

#ifdef DEFRAG
    free(receved_buffer);
#endif

    return status;
}

static bool InitSimpleSocketServer(int *sock_result, const char *hostPort)
{   
    int sockfd, res;
    unsigned int rcvbuf = RCVBUF_SIZE;
    struct addrinfo hints, *result;

    *sock_result = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; /* TODO: check if needed with SOCK_DGRAM */
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    res = getaddrinfo(NULL, hostPort, &hints, &result);

    if (res != 0)
    {
        fprintf(stderr, "Cannot connect to server: getaddinfo %s\n", gai_strerror(res));
        exit(EXIT_FAILURE);
    }

    if (result == NULL)
    {
        fprintf(stderr, "Cannot connect to server: kernel returned NULL\n");
        exit(EXIT_FAILURE);
    }

    CHECK_NOT_M1(sockfd, socket(AF_INET, SOCK_DGRAM, 0), "Simple Socket failed");
    
    CHECK_NOT_M1(res, bind(sockfd, result->ai_addr, result->ai_addrlen), "Simple socket bind failed");
    
    freeaddrinfo(result);

    *sock_result = sockfd;

    if (setsockopt(sockfd,
                   SOL_SOCKET,
/* 128KB */
#if RCVBUF_SIZE <= 131072
                   SO_RCVBUF,
#else
                   SO_RCVBUFFORCE,
#endif
                   &rcvbuf, sizeof(rcvbuf)) < 0)
    {
        close(sockfd);
        perror("Error in setting maximum receive buffer size"
#if RCVBUF_SIZE > 131072
               ". Run as sudo? "
#endif
               );

        return false;
    }

    return true;
}

static bool InitSimpleSocketClient(int *sock_result, const char *hostIP, const char *hostPort)
{   
    int sockfd, res;
    struct addrinfo hints, *result;

    *sock_result = -1;

    CHECK_NOT_M1(sockfd, socket(AF_INET, SOCK_DGRAM, 0), "Simple Socket failed");

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    res = getaddrinfo(hostIP, hostPort, &hints, &result);

    if (res != 0)
    {
        fprintf(stderr, "Cannot connect to server: getaddinfo %s\n", gai_strerror(res));
        exit(EXIT_FAILURE);
    }

    if (result == NULL)
    {
        fprintf(stderr, "Cannot connect to server: kernel returned NULL\n");
        exit(EXIT_FAILURE);
    }

    res = connect(sockfd, result->ai_addr, result->ai_addrlen);

    freeaddrinfo(result);

    if (res < 0)
    {
        close(sockfd);
        perror("Cannot connect to server [errno]:");

        switch (errno)
        {
            case ECONNREFUSED:
            case EALREADY:
            case ETIMEDOUT:
            case EINTR:
            case ENETUNREACH:
                return false;
            default:
                exit(EXIT_FAILURE);
        }
    }

    *sock_result = sockfd;

    return true;
}

#ifdef SERVER

static void ServerTransferLoop(const char *controller_ip, const char *robot_ip, const char *port1, const char *port2, const char *port3, const char *port4)
{
    connection_t connection;
    struct pollfd ufds[3];
    int rv;
    
    connection.wrapper_socket_server = -1;
    connection.wrapper_socket_client = -1;
    connection.simple_socket_server = -1;
    connection.simple_socket_client = -1;
    connection.control_socket_server = -1;
    connection.control_socket_client = -1;

    CHECK_RESULT(InitSimpleSocketClient(&connection.simple_socket_client, controller_ip, port1),
                 "Server: Init simple socket client");

    CHECK_RESULT(InitSimpleSocketServer(&connection.simple_socket_server, port2),
                 "Server: Init simple socket server");

    CHECK_RESULT(InitSimpleSocketClient(&connection.wrapper_socket_client, robot_ip, port3),
                 "Server: Init wrapper socket client");

    CHECK_RESULT(InitSimpleSocketServer(&connection.wrapper_socket_server, port4),
                 "Server: Init wrapper socket server");

#ifdef CONTROL
    CHECK_RESULT(InitSimpleSocketClient(&connection.control_socket_client, "localhost", port5),
                 "Server: Init contorl socket client");

    CHECK_RESULT(InitSimpleSocketServer(&connection.control_socket_server, port6),
                 "Server: Init contorl socket server");
#endif

#ifdef DEFRAG
    init_collectors();
#endif

    printf("Init done!\n");

    ufds[0].fd = connection.wrapper_socket_server;
    ufds[0].events = POLLIN;
    ufds[1].fd = connection.simple_socket_server;
    ufds[1].events = POLLIN;
#ifdef CONTROL
    ufds[2].fd = connection.control_socket_server;
    ufds[2].events = POLLIN;
#endif

    for (;;)
    {
        CHECK_NOT_M1(rv, poll(ufds,
#ifdef CONTROL
                    3
#else
                    2
#endif
                    , 10000), "Transfer loop - poll failed");

        if (rv == 0) /*Timeout!*/
        {
            /*TODO: ka*/;
            DEBUG(printf("Poll timeout!\n");)
        }
        else
        {
            if (ufds[0].revents & POLLIN)
            {
                HandleWrapperServer(&connection);
            }

            if (ufds[1].revents & POLLIN)
            {
                HandleSimpleServer(&connection);    
            }

#ifdef CONTROL
            if (ufds[2].revents & POLLIN)
            {
                HandleControl(&connection);
            }
#endif
        }
    }
}

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

#if defined(RPI)
    ServerTransferLoop(argv[1], argv[2], argv[3], argv[3], argv[4], argv[4]);
#else
    ServerTransferLoop(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
#endif

    return EXIT_SUCCESS;
}

#else /* CLIENT */

static void ServerTransferLoop(const char *server_ip, const char *port1, const char *port2, const char *port3, const char *port4)
{
    connection_t connection;
    struct pollfd ufds[3];
    int rv;
    
    connection.wrapper_socket_server = -1;
    connection.wrapper_socket_client = -1;
    connection.simple_socket_server = -1;
    connection.simple_socket_client = -1;

    CHECK_RESULT(InitSimpleSocketClient(&connection.simple_socket_client, "localhost", port1),
                 "Server: Init simple socket client");

    CHECK_RESULT(InitSimpleSocketServer(&connection.simple_socket_server, port2),
                 "Server: Init simple socket server");

    CHECK_RESULT(InitSimpleSocketClient(&connection.wrapper_socket_client, server_ip, port3),
                 "Server: Init wrapper socket client");

    CHECK_RESULT(InitSimpleSocketServer(&connection.wrapper_socket_server, port4),
                 "Server: Init wrapper socket server");

#ifdef DEFRAG
    init_collectors();
#endif

    printf("Init done!\n");

    ufds[0].fd = connection.wrapper_socket_server;
    ufds[0].events = POLLIN;
    ufds[1].fd = connection.simple_socket_server;
    ufds[1].events = POLLIN;
    
    for (;;)
    {
        CHECK_NOT_M1(rv, poll(ufds, 3, 10000), "Transfer loop - poll failed");

        if (rv == 0) /*Timeout!*/
        {
            /*TODO: ka*/;
            DEBUG(printf("Poll timeout!\n"));
        }
        else
        {
            if (ufds[0].revents & POLLIN)
            {
                HandleWrapperServer(&connection);
            }

            if (ufds[1].revents & POLLIN)
            {
                HandleSimpleServer(&connection);    
            }
        }
    }
}

static bool is_parent = false;
static pid_t cpid = 0;

static void cleanup_handler(int sig,
                            __attribute__ ((unused)) siginfo_t *si,
                            __attribute__ ((unused)) void *unused)
{
    printf("Caught(%d): %d\n", getpid(), sig);

    reporter_cleanup();

    unlink("k/ooditto");

    if (is_parent)
    {
        DEBUG(puts("Killing child..."));
        kill(cpid, SIGTERM);
    }

    if (sig == SIGINT)
    {
        system("rm -rf tools " FILES_DIR);
    }
    else if (sig == SIGSEGV)
    {
        exit(-42);
    }
}

int start_app(int argc, const char *argv[])
{
    struct sigaction sa;

    if (argc != CLIENT_ARGUMENTS)
    {
        printf("Usage:\r\n"
#if defined(RPI) && !defined(SERVER)
               "  %s <Server IP> <Server Port>"
#else
               "  %s <Server IP> <Server Port SEND> <Server Port RECV>"
#endif 
               " <Simple SEND port (To Robot controller process)>"
               " <Simple RECV port (from Robot controller process)>\n", argv[0]);

        return EXIT_FAILURE;
    }

    if (!reporter_init(REPORT_IP, REPORT_PORT))
    {
        perror("main: reporter_init()");

        return EXIT_FAILURE;
    }

    wiringPiSetup();

    if (!getuid())
    {
        setuid(1000);
    }

    /* Set LED pins */
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(ORANGE_PIN, OUTPUT);
    pinMode(RED_PIN, OUTPUT);

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = cleanup_handler;

    if (sigaction(SIGSEGV, &sa, NULL) < 0)
    {
        perror("main: sigaction() SIGSEGV");

        return EXIT_FAILURE;
    }

    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        perror("main: sigaction() SIGINT");

        return EXIT_FAILURE;
    }

    /* Remove tools and k directories */
    system("rm -rf tools " FILES_DIR);

    /* Setup the env for usage */
    if (mkdir(FILES_DIR, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0)
    {
        perror("main: mkdir() " FILES_DIR);

        return EXIT_FAILURE;
    }

    if (mkdir("tools", S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0)
    {
        perror("main: mkdir() tools");

        return EXIT_FAILURE;
    }

    symlink("../tools", "k/ooditto");

    for (;;)
    {
        if ((cpid = fork()) < 0)
        {
            perror("main: fork()");

            return EXIT_FAILURE;
        }

        if (cpid == 0)
        {
#if defined(RPI) && !defined(SERVER)
            ServerTransferLoop(argv[1], argv[3], argv[4], argv[2], argv[2]);
#else
            ServerTransferLoop(argv[1], argv[4], argv[5], argv[2], argv[3]);
#endif        
        }
        else
        {
            pid_t w;
            int status;

            is_parent = true;

            printf("Waiting...\n");

            if ((w = wait(&status)) < 0)
            {
                perror("main: wait()");

                return EXIT_FAILURE;
            }

            printf("Signal caught\n");

            if (WIFEXITED(status))
            {
                printf("Client exited with status %d\n", WEXITSTATUS(status));

                if (WEXITSTATUS(status) == 0)
                {
                    break;
                }
            }

            if (WIFSIGNALED(status))
            {
                printf("Client killed by signal %d\n", WTERMSIG(status));
            }

            BLINK(RED_PIN, 5, 0);
        }

        REPORT();
    }

    cleanup_handler(0, NULL, NULL);

    return EXIT_SUCCESS;
}

#endif

int main(int argc, const char *argv[])
{
    DEBUG(printf("%d\n", getpid()));

    return start_app(argc, argv);
}

