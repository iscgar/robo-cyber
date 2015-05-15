#ifndef SERVER /* CLIENT */

#include <stdio.h>
#include <stdbool.h> /*for bool type, true and false values*/
#include <unistd.h> /*sleep, getpid*/
#include <fcntl.h>
#include <sys/wait.h> /*wait*/
#include <signal.h> /* for destruction */
#include "common.h"
#include "report.h"

#define TARGET_IP_INDEX 1

#if defined(RPI)
#   define TARGET_SEND_PORT_INDEX 2
#   define TARGET_RECV_PORT_INDEX 2
#   define ROBOT_SEND_PORT_INDEX 3
#   define ROBOT_RECV_PORT_INDEX 4
#else
#   define TARGET_SEND_PORT_INDEX 2
#   define TARGET_RECV_PORT_INDEX 3
#   define ROBOT_SEND_PORT_INDEX 4
#   define ROBOT_RECV_PORT_INDEX 5
#endif

static pid_t cpid = 0;

static void cleanup_handler(int sig,
                            __attribute__ ((unused)) siginfo_t *si,
                            __attribute__ ((unused)) void *unused)
{
    DEBUG(printf("Caught(%d): %d\n", getpid(), sig));

    reporter_cleanup();

    if (cpid != 0)
    {
        DEBUG(puts("Killing child..."));
        kill(cpid, SIGTERM);

        if (sig == SIGINT)
        {
            unlink("k/ooditto");
            system("rm -rf " TOOLS_DIR " " FILES_DIR);
            exit(EXIT_SUCCESS);
        }
    }
    else
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

    if (!reporter_init(argv[TARGET_IP_INDEX], REPORT_PORT))
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
    system("rm -rf " TOOLS_DIR " " FILES_DIR);

    /* Setup the env for usage */
    if (mkdir(FILES_DIR, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0)
    {
        perror("main: mkdir() " FILES_DIR);
        return EXIT_FAILURE;
    }

    if (mkdir(TOOLS_DIR, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0)
    {
        perror("main: mkdir() " TOOLS_DIR);
        return EXIT_FAILURE;
    }

    symlink("../" TOOLS_DIR, "k/ooditto");

    for (;;)
    {
        if ((cpid = fork()) < 0)
        {
            perror("main: fork()");
            return EXIT_FAILURE;
        }

        if (cpid == 0) /* Child */
        {
            StartTransferLoop("localhost",
                              argv[TARGET_IP_INDEX],
                              argv[ROBOT_SEND_PORT_INDEX],
                              argv[ROBOT_RECV_PORT_INDEX],
                              argv[TARGET_SEND_PORT_INDEX],
                              argv[TARGET_RECV_PORT_INDEX]);
        }
        else
        {
            pid_t w;
            int status;

            DEBUG(printf("Waiting...\n"));

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
    }

    return EXIT_SUCCESS;
}

#endif