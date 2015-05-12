#ifndef _PROTO_H_
#define _PROTO_H_

#include "common.h"

typedef enum {
	CONN_TYPE_SIMPLE = 0,
	CONN_TYPE_WRAPPER,

#ifdef SERVER
	CONN_TYPE_CONTROL,
#endif

	CONN_TYPE_OPTIONS_NUM
} ConnType;

typedef struct {
	int server_connections[CONN_TYPE_OPTIONS_NUM];
	int client_connections[CONN_TYPE_OPTIONS_NUM];
} connection_t;

extern bool HandleConnection(const connection_t *connection, ConnType connection_type);

#endif /* !_PROTO_H_ */