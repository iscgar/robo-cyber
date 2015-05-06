#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define MAX_PACKET_TOTAL (1024 * 16)
#define RCVBUF_SIZE (1024 * 128)
#define MAX_PACKET_SIZE 64 /* 823 */
#define TEMP_STR_SIZE   32

#define BLUE_PIN 7
#define ORANGE_PIN 8
#define RED_PIN 0

#define NORMAL_BLINK 10000

#define DEBUG(X) X
#define to "malloc"

#define CMD_LEN 128
#define MAX_BUFFER 1400

#define FILES_DIR "k"
#define CHECK_PROG "./tools/check.py"

#if defined(RPI)
#   if !defined(SERVER)
#       define CLIENT_ARGUMENTS 5
#   else
#       define SERVER_ARGUMENTS 5
#   endif
#else
#   define CLIENT_ARGUMENTS     6
#   define SERVER_ARGUMENTS     7
#endif

#ifdef DEFRAG
#   define MAX_COLLECTORS 128
#endif

#if defined(RPI) && !defined(SERVER)
#   include <wiringPi.h>
#   define BLINK(led, time, utime)  \
            digitalWrite((led), HIGH); \
            usleep((utime));           \
            sleep((time));             \
            digitalWrite((led), LOW);
#else
#   define BLINK(led, time, utime)  printf("LED: %d\n", (led))
#   define pinMode(x, y)
#   define wiringPiSetup()
#endif

#define REPORT_IP   "localhost"
#define REPORT_PORT 1313

#define UNLIKELY(x) __builtin_expect((x), 0)

#define CHECK_OR_DIE(res, expression, expected, msg) \
            { \
                res = expression;             \
                if(UNLIKELY(res == expected)) \
                {                             \
                    perror(msg);              \
                    exit(EXIT_FAILURE);       \
                }                             \
            }       

#define CHECK_NOT_M1(res, e, msg) CHECK_OR_DIE(res, e, -1, msg)

#define CHECK_RESULT(res, msg)                  \
            if (res)                            \
            {                                   \
                printf("%s succeeded\n", msg);  \
            }                                   \
            else                                \
            {                                   \
                printf("%s failed\n", msg);     \
                exit(EXIT_FAILURE);             \
            }

typedef struct hdr_t
{
    uint32_t src;
    uint32_t dst;
    uint32_t size;
    uint32_t id;
    uint32_t frag_idx;
} hdr_t;

typedef enum
{
    E_ERR,
    E_FRAG,
    E_SUCCESS
} frag_e;

void print_hex(const uint8_t *buf, uint32_t size);

#endif /* !COMMON_H */