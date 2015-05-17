#define _GNU_SOURCE /* memrchr() */

#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
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

bool normalize_path(const char *src, char *o_dst, size_t dst_len)
{
    char res[PATH_MAX];
    size_t res_len, src_len;
    const char *ptr = src;
    const char *end, *next;

    if ((o_dst == NULL) || (dst_len == 0))
    {
        return false;
    }

    if ((src_len = strnlen(src, PATH_MAX)) >= PATH_MAX)
    {
        return false;
    }

    end = &src[src_len];

    if ((src_len == 0) || (*ptr != '/'))
    {
        /* relative path */
        if (getcwd(res, sizeof(res)) == NULL)
        {
            return NULL;
        }

        res_len = strlen(res);
    }
    else
    {
        res_len = 0;
    }

    for (ptr = src; ptr < end; ptr = next + 1)
    {
        size_t len;
        next = memchr(ptr, '/', end - ptr);

        if (next == NULL)
        {
            next = end;
        }

        len = next - ptr;

        switch(len) 
        {
            case 2:
            {
                if ((*ptr == '.') && (ptr[1] == '.'))
                {
                    const char *slash = memrchr(res, '/', res_len);

                    if (slash != NULL)
                    {
                        res_len = slash - res;
                    }

                    continue;
                }

                break;
            }
            case 1:
            {
                if (*ptr == '.')
                {
                    continue;
                }
             
                break;
            }
            case 0:
            {
                continue;
            }
        }

        res[res_len++] = '/';
        memcpy(&res[res_len], ptr, len);
        res_len += len;
    }

    if (res_len == 0)
    {
        res[res_len++] = '/';
    }

    res[res_len++] = '\0';

    if (dst_len < res_len)
    {
        return false;
    }

    memcpy(o_dst, res, res_len);

    return true;
}
