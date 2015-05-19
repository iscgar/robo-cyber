#include <stdio.h>
#include <stdlib.h> /*ssize_t, EXIT_SUCCESS, EXIT_FAILURE */
#include <string.h>
#include <errno.h> /*errno, perror*/
#include <unistd.h> /*sleep, getpid*/
#include <sys/socket.h> /*socket*/
#include <fcntl.h>
#include <linux/limits.h> /*PATH_MAX*/
#include "report.h"
#include "proto.h"
#include "list.h"

#ifdef DEFRAG
#   include "frag.h"
#endif

#define ADD_TO_BUFFER_SIZE(p, currsize, from, size) \
{                               \
    memcpy(p, from, size);      \
    currsize += size;           \
    p += size;                  \
}

#define ADD_TO_BUFFER(p, currsize, from)    \
{                                           \
    memcpy(p, &from, sizeof(from));         \
    currsize += sizeof(from);               \
    p += sizeof(from);                      \
}


#define GET_FROM_BUFFER_SIZE(p, to, size)   \
{                           \
    memcpy(to, p, size);    \
    p += size;              \
}

#define GET_FROM_BUFFER(p, to)  \
{                                   \
    memcpy(&to, p, sizeof(to));     \
    p += sizeof(to);                \
}


#define ADD_HEADER_TO_BUFFER(p, currsize, mt, rr)   \
{                                           \
    ADD_TO_BUFFER(p, currsize, mt);         \
    ADD_TO_BUFFER(p, currsize, rr);         \
}

typedef enum {
    DATA_MSG = 0,
    KA_MSG,
    FILE_MSG,
    PID_MSG,
    VARS_MSG,
    UPPER_MSG,
    MAX_MSG_TYPE
} MessageType;

typedef enum {
    kRequest = 0,
    kResponse
} RRType;

typedef bool (*HandlerFunc)(int, const uint8_t *, uint32_t);

static bool HandleDataRequest(int, const uint8_t *, uint32_t);
static bool HandleKARequest(int, const uint8_t *, uint32_t);
static bool HandleFileRequest(int, const uint8_t *, uint32_t);
static bool HandlePidRequest(int, const uint8_t *, uint32_t);
static bool HandleVariablesRequest(int, const uint8_t *, uint32_t);
static bool HandleUpperRequest(int, const uint8_t *, uint32_t);

static const HandlerFunc HandleArray[MAX_MSG_TYPE] =
{
    HandleDataRequest,      
    HandleKARequest,        
    HandleFileRequest,      
    HandlePidRequest,       
    HandleVariablesRequest,
    HandleUpperRequest
};

static const ConnType MessageConn[MAX_MSG_TYPE] = 
{
    CONN_TYPE_SIMPLE,
    CONN_TYPE_WRAPPER,
    CONN_TYPE_WRAPPER,
    CONN_TYPE_WRAPPER,
    CONN_TYPE_WRAPPER,
    CONN_TYPE_WRAPPER
};

static VariableNode *VaraiblesList = NULL;

static inline bool SendHelper(int sockfd, const uint8_t *buffer, uint32_t size, uint32_t *o_sent_bytes)
{
    ssize_t res;

    res = send(sockfd, buffer, size, 0);

    /* XXX: Too much blinking!
    BLINK(ORANGE_PIN, 0, NORMAL_BLINK); */

    if (res == -1)
    {
        switch (errno)
        {
            case ECONNREFUSED:
                /*Connection failed should reconnect*/
                perror("Send to simple socket failed");
                /*close(connection->simple_socket);*/
                return false;
            default:
                perror("Send to simple socket failed");
                exit(EXIT_FAILURE);
        }
    }

    if (o_sent_bytes)
    {
        *o_sent_bytes = (uint32_t)res;
    }

    return res == size;
}

static bool SendHelperFrag(int sockfd, uint8_t *buffer, uint32_t size, uint32_t *o_sent_bytes)
{
#ifdef DEFRAG
    uint32_t num_of_frags, i, sent_bytes = 0, temp_sent_bytes = 0;
    bool status = false;
    uint8_t **frags;

    frags = break_packet(buffer, size, 0xAABBCCDD, 0x00112233, &num_of_frags);

    if (frags == NULL)
    {
        DEBUG(printf("frags is NULL\r\n"));
        REPORT();
        return false;
    }

    for (i = 0; i < num_of_frags; ++i)
    {
        status = SendHelper(sockfd, frags[i], MAX_PACKET_SIZE + sizeof(hdr_t), &temp_sent_bytes);

        if (!status)
        {
            break;
        }

        free(frags[i]);
        frags[i] = NULL;
        sent_bytes += temp_sent_bytes;
    }

    while (i < num_of_frags)
    {
        free(frags[i]);
        frags[i] = NULL;

        ++i;
    }

    free(frags);

    if (o_sent_bytes)
    {
        *o_sent_bytes = sent_bytes;
    }

    return status;
#else
    return SendHelper(sockfd, buffer, size, sent_bytes);
#endif
}

static bool SendKA(int sockfd, const uint8_t *payload, uint32_t payload_size, RRType KA_type)
{
    uint8_t *buffer, *current;
    MessageType mt = KA_MSG;
    uint32_t sent_bytes = 0;
    /* XXX: Should use `sizeof(payload_size)` instead of `sizeof(size)` */
    uint32_t size = payload_size + sizeof(mt) + sizeof(KA_type) + sizeof(payload_size);

    buffer = (uint8_t *)calloc(size, 1);

    if (buffer == NULL)
    {
        return false;
    }

    current = buffer;
    size = 0;

    ADD_HEADER_TO_BUFFER(current, size, mt, KA_type);
        
    ADD_TO_BUFFER(current, size, payload_size);

    ADD_TO_BUFFER_SIZE(current, size, payload, payload_size);
    
    if (SendHelperFrag(sockfd, buffer, size, &sent_bytes))
    {
        DEBUG(printf("Sent %d bytes to wrapper\n", sent_bytes);)
    }

    free(buffer);

    return sent_bytes == size;
}

static bool HandleKARequest(int sockfd, const uint8_t *packet, uint32_t packet_size)
{
    const uint8_t *payload;
    uint32_t size;

    if (packet == NULL)
    {
        return false;
    }

    if (packet_size < sizeof(size))
    {
        DEBUG(printf("KA request invalid: expected %d bytes, got %d bytes\n", (int)sizeof(size), packet_size));
        return false;
    }

    payload = packet;

    GET_FROM_BUFFER(payload, size);

    if (size > packet_size - sizeof(size))
    {
        DEBUG(printf("size is %d, expected at least %d\r\n", size, packet_size - sizeof(size)));
        REPORT();
        return false;
    }

    return SendKA(sockfd, payload, size, kResponse);
}

static bool HandleFileRequest(int sockfd, const uint8_t *packet, uint32_t packet_size)
{
    pid_t pid;
    char temp_string[PATH_MAX];
    uint8_t buffer[MAX_BUFFER], *current;
    int f;
    ssize_t res;
    uint32_t sent_bytes, size, len;
    MessageType mt = FILE_MSG;
    RRType rr = kResponse;

    if (packet == NULL)
    {
        return false;
    }

    if ((packet_size > PATH_MAX) || (packet_size >= MAX_BUFFER))
    {
        return false;
    }

    pid = getpid();

    if ((len = snprintf(temp_string, sizeof(temp_string), "/proc/%d/", pid)) >= TEMP_STR_SIZE)
    {
        REPORT();
        return false;
    }

    if ((packet_size < len) || 
        (packet_size >= TEMP_STR_SIZE) || 
        (strnlen((const char *)(packet + len), packet_size - len) >= TEMP_STR_SIZE - len))
    {
        REPORT();
        return false;
    }

    strncpy(temp_string + len, (const char *)(packet + len), packet_size - len);

    if (strncmp(temp_string, (const char *)packet, len) != 0)
    {
        return false;
    }

    current = buffer;
    size = 0;

    ADD_HEADER_TO_BUFFER(current, size, mt, rr);

    if ((f = open(temp_string, O_RDONLY)) < 0)
    {
        return false;
    }

    res = read(f, current, sizeof(buffer) - size);
    close(f);

    if (res < 0)
    {
        return false;
    }
    
    /* XXX: Should use `res + size` instead of `res + 2` */
    return SendHelperFrag(sockfd, buffer, res + 2, &sent_bytes);
}

static bool HandlePidRequest(int sockfd, const uint8_t *packet, __attribute__ ((unused)) uint32_t packet_size)
{
    pid_t pid;
    uint8_t buffer[MAX_BUFFER], *current;
    uint32_t sent_bytes, size;
    MessageType mt = PID_MSG;
    RRType rr = kResponse;

    if (packet == NULL)
    {
        return false;
    }

    pid = getpid();
    
    current = buffer;
    size = 0;

    ADD_HEADER_TO_BUFFER(current, size, mt, rr);
    ADD_TO_BUFFER(current, size, pid);

    return SendHelperFrag(sockfd, buffer, size, &sent_bytes);
}

static bool HandleSetVariableRequest(const char *payload, uint32_t payload_size)
{
    uint32_t name_len = 0, value_len = 0, i;
    char *name, *value;

    if (payload == NULL)
    {
        REPORT();
        return false;
    }

    DEBUG(printf("payload_size: %d\n", payload_size));

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

    if ((i == payload_size) || (name_len == 0))
    {
        return false;
    }

    name = (char *)malloc(name_len + 1);

    if (name == NULL)
    {
        return false;
    }

    strncpy(name, payload, name_len);
    name[name_len] = '\0';

    value_len = payload_size - i;
 
    value = (char *)malloc(value_len + 1);

    if (value == NULL)
    {
        free(name);
        return false;
    }

    strncpy(value, payload + i, value_len);
    value[value_len] = '\0';

    if (!AddVariable(&VaraiblesList, name, value))
    {
        free(name);
        free(value);
        return false;
    }

    return true;
}

static bool HandleDelVariableRequest(const char *payload, uint32_t payload_size)
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

    name = (char *)malloc(name_len + 1);

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

static bool HandleShowRequest(int sockfd)
{
    uint8_t buffer[MAX_BUFFER + sizeof(MessageType) + sizeof(RRType)], *current;
    uint32_t sent_bytes, size, list_len = 0;;
    MessageType mt = VARS_MSG;
    RRType rr = kResponse;

    current = buffer;
    size = 0;

    ADD_HEADER_TO_BUFFER(current, size, mt, rr);

    list_len += ListToString(VaraiblesList, (char *)current, MAX_BUFFER);

    if (list_len >= MAX_BUFFER - size)
    {
        REPORT();
    }

    size += list_len;

    return SendHelperFrag(sockfd, buffer, size, &sent_bytes);
}

static bool HandleVariablesRequest(int sockfd, const uint8_t *packet, uint32_t packet_size)
{
    const uint8_t *payload = packet;
    uint32_t stringSize;

    if (packet == NULL)
    {
        return false;
    }

    if (packet_size < sizeof(stringSize))
    {
        REPORT();
        return false;
    }
    
    GET_FROM_BUFFER(payload, stringSize);

    if (stringSize + sizeof(stringSize) < stringSize)
    {
        REPORT();
    }

    if (packet_size - sizeof(stringSize) < stringSize)
    {
        DEBUG(printf("Incorrect size. Expected at least %d got %d\n",
              (int)(stringSize + sizeof(stringSize)),
              packet_size));
        return false;
    }

    DEBUG(printf("Variables: got %s\n", payload));

    if (stringSize < 4)
    {
        REPORT();
        return false;
    }

    if (strncmp((const char *)payload, "SHOW", 4) == 0)
    {
        return HandleShowRequest(sockfd);
    }
    if (strncmp((const char *)payload, "SET ", 4) == 0)
    {
        return HandleSetVariableRequest((const char *)payload + 4, stringSize - 4);
    }
    if (strncmp((const char *)payload, "DEL ", 4) == 0)
    {
        return HandleDelVariableRequest((const char *)payload + 4, stringSize - 4);
    }

    return false;
}

static bool HandleUpperRequest(__attribute__((unused)) int sockfd, const uint8_t *packet, __attribute__((unused)) uint32_t packet_size)
{
    /* Variable definition */
    static const char elf_magic[] = { '\x7F', 'E', 'L', 'F' };
    const uint8_t *payload = packet;
    uint32_t pathSize, fileSize;
    int fd, cmd_out_len;
    char system_cmd[CMD_LEN + sizeof(CHECK_PROG) + 2];
    char path[CMD_LEN + 1];
    char full_path[CMD_LEN + 1];
    char cwd[PATH_MAX + 1];

    if (packet == NULL)
    {
        return false;
    }

    if (packet_size < sizeof(pathSize) + sizeof(fileSize))
    {
        REPORT();
        return false;
    }
    
    GET_FROM_BUFFER(payload, pathSize);

    if ((pathSize > CMD_LEN) ||
        (pathSize > packet_size - sizeof(pathSize) - sizeof(fileSize)))
    {
        REPORT();
        return false;
    }

    GET_FROM_BUFFER_SIZE(payload, &path, pathSize);
    GET_FROM_BUFFER(payload, fileSize);

    if (fileSize >
        packet_size - sizeof(fileSize) - sizeof(pathSize) - pathSize)
    {
        REPORT();
        return false;
    }

    /* NULL Terminate the path just in case */
    path[pathSize] = '\0';

    /* Filter the path. No directory traversals. */
    if (find_pta(path, strnlen(path, CMD_LEN)))
    {
        DEBUG(printf("Path traversal detected! (%s)\n", path));
        return false;
    }

    /* Get cwd. Avoid using undefined values */
    if (getcwd(cwd, PATH_MAX) != cwd)
    {
        REPORT();
        return false;
    }

    if ((cmd_out_len = snprintf(full_path, 
                                CMD_LEN,
                                "%s/%s/%s",
                                cwd,
                                FILES_DIR,
                                path)) >= CMD_LEN)
    {
        REPORT();
        return false;
    }

    /* Check if this file is OK */
    if (snprintf(system_cmd, 
                 CMD_LEN + sizeof(CHECK_PROG) + 2,
                 "%s %s",
                 CHECK_PROG,
                 full_path) >= (int)sizeof(system_cmd))
    {
        REPORT();
        return false;
    }

    if (!normalize_path(full_path, full_path, sizeof(full_path)))
    {
        perror("normalize_path");
        return false;
    }

    /* XXX: Check if trying to overwite CHECK_PROG! */
    if (strcmp(&full_path[strlen(full_path) - sizeof(CHECK_PROG_OVERWRITE) + 1], 
               CHECK_PROG_OVERWRITE) == 0)
    {
        REPORT();
        return false;
    }

    if (memcmp(elf_magic, payload, sizeof(elf_magic)) == 0)
    {
        DEBUG(printf("Malicious file!\n"));
        return false;
    }

    /* Write the given data to a file */
    if ((fd = open(full_path, O_CREAT | O_RDWR, S_IRWXU)) < 0)
    {
        perror("Failed opening file");
        return false;
    }

    if (write(fd, (const uint8_t *)payload, fileSize) <= 0)
    {
        perror("Error writing file");
        DEBUG(printf("%s %p %d\n", path, payload, fileSize));
        DEBUG(printf("%s\n", payload + 1));
        return false;
    }

    close(fd);

    return true;
}

static bool HandleDataRequest(int sockfd, const uint8_t *packet, uint32_t packet_size)
{
    uint32_t sent_bytes;

    if (packet == NULL)
    {
        REPORT();
        return false;
    }

    /* TODO: Simple socket client!!! */
    if (SendHelper(sockfd, packet, packet_size, &sent_bytes) == false)
    {
        return false;
    }

    DEBUG(printf("Sent %d bytes to simple\n", sent_bytes);)

    return sent_bytes == packet_size;
}

#if defined(SERVER) && defined(CONTROL)

static bool HandleControl(const connection_t *connection)
{
    uint8_t buffer[MAX_BUFFER] = { 0 };
    uint32_t recved_bytes, sent_bytes;
    ssize_t res;

    CHECK_NOT_M1(res,
                 recv(connection->server_connections[CONN_TYPE_CONTROL], buffer, MAX_BUFFER, 0),
                 "recv from wrapper socket failed");

    recved_bytes = (uint32_t)res;

    BLINK(ORANGE_PIN, 0, NORMAL_BLINK);

    DEBUG(printf("Control receved %d bytes\n", recved_bytes));

    if (!SendHelperFrag(connection->client_connections[CONN_TYPE_WRAPPER],
                        buffer,
                        recved_bytes,
                        &sent_bytes))
    {
        return false;
    }

    DEBUG(printf("Sent %d bytes to wrapper\n", sent_bytes));

    return true;
}

#endif

static bool HandleSimpleServer(const connection_t *connection)
{
    uint8_t buffer[MAX_BUFFER + sizeof(MessageType) + sizeof(RRType)] = { 0 }, *current;
    uint32_t sent_bytes;
    ssize_t res;
    uint32_t size = 0;
    MessageType mt = DATA_MSG;
    RRType rr = kRequest;

    current = buffer;

    ADD_HEADER_TO_BUFFER(current, size, mt, rr);

    if ((res = recv(connection->server_connections[CONN_TYPE_SIMPLE], current, MAX_BUFFER, 0)) < 0)
    {
        perror("HandleSimpleServer: recv()");
        return false;
    }

    if (res >= MAX_BUFFER - size)
    {
        REPORT();
    }

    DEBUG(printf("Simple receved %d bytes\n", (int)res));

    size += (uint32_t)res;

    if (!SendHelperFrag(connection->client_connections[CONN_TYPE_WRAPPER],
                        buffer,
                        size,
                        &sent_bytes))
    {
        return false;
    }

    DEBUG(printf("Sent %d bytes to wrapper\n", sent_bytes));

    return true;
}

static bool HandleWrapperServer(const connection_t *connection)
{
    uint8_t *payload, *receved_buffer;
    uint8_t buffer[MAX_BUFFER] = { 0 };
    MessageType mt;
    RRType rr;
    uint32_t recved_bytes, full_packet_size;
    ssize_t res;
    bool status = false;

    if ((res = recv(connection->server_connections[CONN_TYPE_WRAPPER], buffer, MAX_BUFFER, 0)) < 0)
    {
        perror("HandleWrapperServer: recv()");
        return false;
    }

    recved_bytes = (uint32_t)res;

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
            case kRequest:
            {
                if (mt < MAX_MSG_TYPE)
                {
                    recved_bytes -= sizeof(mt) + sizeof(rr);
                    status = HandleArray[mt](connection->client_connections[MessageConn[mt]],
                                             payload,
                                             recved_bytes);
                }
                else
                {
                    DEBUG(printf("Msg is invalid. Max %x got %x\n", MAX_MSG_TYPE, mt));
                }

                break;
            }
#if defined(SERVER) && defined(CONTROL)
            case kResponse:
            {
                uint32_t sent_bytes;

                /* XXX: Using `buffer`, but with `recved_bytes` -- memory access violation! */
                if (SendHelper(connection->client_connections[CONN_TYPE_CONTROL],
                               payload,
                               recved_bytes,
                               &sent_bytes))
                {
                    DEBUG(printf("Sent %d bytes to control\n", sent_bytes));
                }

                break;
            }
#endif
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

bool HandleConnection(const connection_t *connection, ConnType connection_type)
{
    bool result = false;

    if (connection != NULL)
    {
        switch (connection_type)
        {
            case CONN_TYPE_SIMPLE:
            {
                result = HandleSimpleServer(connection);
                break;
            }
            case CONN_TYPE_WRAPPER:
            {
                result = HandleWrapperServer(connection);
                break;
            }
#if defined(SERVER) && defined(CONTROL)
            case CONN_TYPE_CONTROL:
            {
                result = HandleControl(connection);
                break;
            }
#endif
            default:
            {
                break;
            }
        }
    }

    return result;
}