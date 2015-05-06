#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

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
