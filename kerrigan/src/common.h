#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h> /*for bool type, true and false values*/

#define MAX_PACKET_TOTAL (1024 * 16)
#define RCVBUF_SIZE (1024 * 128)
#define MAX_PACKET_SIZE 64 /* 823 */
#define TEMP_STR_SIZE   32

#define BLUE_PIN 2
#define ORANGE_PIN 3
#define RED_PIN 0

#define NORMAL_BLINK 10000

#ifndef RELEASE
#   define DEBUG(X) X
#else
#   define DEBUG(X)
#endif

#define CMD_LEN 128
#define MAX_BUFFER 1400

#define FILES_DIR "k"
#define TOOLS_DIR "tools"
#define FILES_TOOLS_SYMLINK FILES_DIR "/ooditto"
#define CHECK_PY "check.py"
#define CHECK_PROG "./" TOOLS_DIR "/" CHECK_PY
#define CHECK_PROG_OVERWRITE FILES_TOOLS_SYMLINK "/" CHECK_PY

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
#   include <time.h>
#   define BLINK(led, time, utime)  \
    { \
        struct timespec t;          \
        t.tv_sec = (time);          \
        t.tv_nsec = (utime) * 1000; \
        digitalWrite((led), HIGH);  \
        nanosleep(&t, NULL);        \
        digitalWrite((led), LOW); \
    }
#else
#   define BLINK(led, time, utime)  DEBUG(printf("LED: %d\n", (led)))
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

extern void StartTransferLoop(const char *controller_ip, 
                              const char *target_ip,
                              const char *port1,
                              const char *port2,
                              const char *port3,
                              const char *port4
#if defined(SERVER) && defined(CONTROL)
                              , const char *port5,
                              const char *port6
#endif
                              );

extern int start_app(int argc, const char *argv[]);

extern void print_hex(const uint8_t *buf, uint32_t size);
extern bool find_pta(const char *path, uint32_t len);
extern bool normalize_path(const char *src, char *o_dst, size_t dst_len);

#endif /* !COMMON_H */
