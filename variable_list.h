#ifndef VARIABLE_LIST_H
#define VARIABLE_LIST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct VariableNode_t
{
	const char *name;
	const char *value;
	struct VariableNode_t *next;
	struct VariableNode_t *prev;
} VariableNode;

extern bool AddVariable(VariableNode **firstNode, const char *name, const char *value);
extern void RemoveVariable(VariableNode **node, const char *name);
extern uint32_t ListToString(VariableNode *node, char *buffer, uint32_t buffer_size);

#endif /* !VARIABLE_LIST_H */