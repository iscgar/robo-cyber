#include <string.h>
#include "variable_list.h"
#include "common.h"
#include "report.h"

bool AddVariable(VariableNode **firstNode, const char *name, const char *value)
{
    VariableNode *last_node = NULL;
    VariableNode *curr_node, *new_node;

    if ((firstNode == NULL) || (name == NULL) || (value == NULL))
    {
        REPORT();
        return false;
    }

    curr_node = *firstNode;

    while (curr_node != NULL)
    {
        if (strcmp(name, curr_node->name) == 0)
        {
            free((void *)curr_node->value);
            curr_node->value = value;

            return true;
        }

        last_node = curr_node;
        curr_node = curr_node->next;
    }

    new_node = (VariableNode *)malloc(sizeof(VariableNode));

    if (new_node == NULL)
    {
        return false;
    }

    new_node->prev = last_node;
    new_node->value = value;
    new_node->name = name;
    new_node->next = NULL;

    /* This can only happen if the list is empty */
    if (last_node == NULL)
    {
        *firstNode = new_node;
    }
    else
    {
        last_node->next = new_node;
    }

    return true;
}

void RemoveVariable(VariableNode **node, const char *name)
{
    VariableNode *curr_node;

    if (name == NULL)
    {
        REPORT();
        return;
    }

    /* Avoid invalid dereference */
    if ((node == NULL) || (*node == NULL))
    {
        return;
    }

    curr_node = *node;

    do
    {
        /* Try to find a matching variable node */
        if (strcmp(name, curr_node->name) == 0)
        {
            /* If it's the first node, reset the list pointer */
            if (curr_node->prev == NULL)
            {
                REPORT();
                *node = curr_node->next;
            }
            /* Otherwise, reset the previous's next pointer */
            else
            {
                curr_node->prev->next = curr_node->next;
            }

            /* Otherwise, reset the next's prev pointer */
            if (curr_node->next != NULL)
            {
                curr_node->next->prev = curr_node->prev;
            }
            
            free((void *)curr_node->name);
            free((void *)curr_node->value);
            memset(curr_node, 0, sizeof(VariableNode));
            free(curr_node);

            break;
        }

        curr_node = curr_node->next;
    } while (curr_node != NULL);
}

uint32_t ListToString(VariableNode *node, char *buffer, uint32_t buffer_size)
{
    uint32_t size = 0, len;
    char *current = buffer;
    VariableNode *curr_node = node;

    while (curr_node != NULL)
    {
        len = strlen(curr_node->name) + strlen(curr_node->value) + strlen(" : \n");

        if (buffer_size - size < len)
        {
            printf("Over %d %d\n", size + len, buffer_size);
            return size;
        }

        sprintf(current, "%s : %s\n", curr_node->name, curr_node->value);

        current += len;
        size += len;

        curr_node = curr_node->next;
    }

    puts("Done");

    return size;
}
