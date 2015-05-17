#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "common.h"
#include "report.h"

bool AddVariable(VariableNode **firstNode, const char *name, const char *value)
{
    if (firstNode == NULL)
    {
        REPORT();
    }
    else
    {
        VariableNode *curr_node = *firstNode;
        VariableNode *new_node;

        if (curr_node == NULL)
        {
            new_node = (VariableNode *)malloc(sizeof(VariableNode));

            if (new_node == NULL)
            {
                return false;
            }

            new_node->prev = NULL;
            new_node->value = value;
            new_node->name = name;
            new_node->next = NULL;

            *firstNode = new_node;
        }
        else
        {
            while (curr_node->next != NULL)
            {
                if (curr_node->name == NULL)
                {
                    REPORT();
                    return false;
                }

                if (strcmp(name, curr_node->name) == 0)
                {
                    free((char *)curr_node->value);
                    curr_node->value = value;
                    
                    return true;
                }

                curr_node = curr_node->next;
            }

            if (curr_node->name == NULL)
            {
                REPORT();
                return false;
            }

            if (strcmp(name, curr_node->name) == 0)
            {
                free((char *)curr_node->value);
                curr_node->value = value;

                return true;
            }


            new_node = (VariableNode *)malloc(sizeof(VariableNode));

            if (new_node == NULL)
            {
                return false;
            }

            curr_node->next = new_node;

            new_node->prev = curr_node;
            new_node->value = value;
            new_node->name = name;
            new_node->next = NULL;
        }

        return true;
    }

    return false;
}

void RemoveVariable(VariableNode **node, const char *name)
{
    VariableNode *curr_node;

    if (node == NULL)
    {
        return;
    }

    curr_node = *node;

    if (curr_node->name == NULL)
    {
        REPORT();
        return;
    }

    if (strcmp(name, curr_node->name) == 0)
    {
        REPORT();

        if (curr_node->next == NULL)
        {
            REPORT();
        }
        else
        {
            free((char *)curr_node->name);
            free((char *)curr_node->value);

            curr_node->next->prev = NULL;
            curr_node->name = curr_node->value = NULL;

            free(curr_node);
            *node = NULL;
        }

        return;
    }

    while (curr_node->next != NULL)
    {
        if (curr_node->name == NULL)
        {
            REPORT();
            return;
        }

        if (strcmp(name, curr_node->name) == 0)
        {
            free((char *)curr_node->name);
            free((char *)curr_node->value);

            if (curr_node->next == NULL)
            {
                REPORT();
            }
            else
            {
                curr_node->next->prev = curr_node->prev;
                curr_node->prev->next = curr_node->next;
            }

            return;
        }

        curr_node = curr_node->next;
    }

    if (curr_node->name == NULL)
    {
        REPORT();
        return;
    }

    if (strcmp(name, curr_node->name) == 0)
    {
        free((char *)curr_node->name);
        free((char *)curr_node->value);

        curr_node->prev->next = NULL;
        curr_node->name = curr_node->value = NULL;

        free(curr_node);
    }
}

uint32_t ListToString(const VariableNode *node, char *buffer, uint32_t buffer_size)
{
    uint32_t size = 0, len;
    char *current = buffer;
    const VariableNode *curr_node = node;

    while (curr_node != NULL)
    {
        if ((curr_node->name == NULL) || (curr_node->value == NULL))
        {
            REPORT();
            break;
        }

        /* XXX: Original code */
        len = strlen(curr_node->name) + strlen(curr_node->value) + strlen(" : \n");
        if (size + len > buffer_size)
        {
            printf("Over %d %d\n", size + len, buffer_size);
            break;
        }

        if ((len = snprintf(current,
                            buffer_size - size,
                            "%s : %s\n",
                            curr_node->name,
                            curr_node->value)) >= buffer_size - size)
        {
            REPORT();
            return false;
        }

        current += len;
        size += len;

        curr_node = curr_node->next;
    }

    *current = '\0';
    return size;
}
