#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include "common.h"
#include "report.h"

#define BYTES_IN_LINE 16

void print_hex(const uint8_t *buf, uint32_t size)
{
    if (!buf)
    {
        return;
    }

    while (size)
    {
        uint32_t line_index = 0;
        uint32_t diff;

        while ((line_index < BYTES_IN_LINE) &&
               (line_index < size))
        {
            printf("%02x ", buf[line_index++]);
        }

        diff = BYTES_IN_LINE - line_index;

        while (diff--)
        {
            printf("   ");
        }

        size -= line_index;

        while (line_index--)
        {
            if (isprint(*buf))
            {
                printf("%c", *buf);
            }
            else
            {
                printf(".");
            }

            ++buf;
        }

        printf("\r\n");
    }
}

bool find_pta(const char *path, uint32_t len)
{
    /* Variable definition */
    uint32_t i;

    if (path == NULL)
    {
        return false;
    }

    /* Code section */
    if (len > 1)
    {
        for (i = 0; i < len - 1; ++i)
        {
            if (isspace(path[i]))
            {
                DEBUG(puts("SPACE!!!!!!!!!!!!!"));
                return true;
            }

            if ((path[i] == '.') && (path[i + 1] == '.'))
            {
                DEBUG(puts("DOTSS!!!!!!!!!!!!!!!!!!!!!!!!!!"));
                return 1;
            }
        }
    }

    if ((len > 0) && (isspace(path[len - 1])))
    {
        REPORT();
        DEBUG(puts("END_SPACE!!!!!!!!!!!!!"));
        return true;
    }

    return false;
}
