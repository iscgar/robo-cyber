CC=gcc

# General compiler flags
CFLAGS=-fstack-protector-all -s -fvisibility=hidden -fPIC -Wall -Wextra -Wno-write-strings
LFLAGS=-fPIE -pie -lrt

# Sources list
SOURCES = $(wildcard *.c)

# Defs
export DEFS:=-DDEFRAG

ifdef RELEASE
DEFS+=-DRELEASE
endif

ifneq ($(EMULATE),EMULATE)
DEFS+=-DRPI
CLIENT_LFLAGS:=-lwiringPi
else
CLIENT_LFLAGS :=
endif

# Set the object file names, with the source directory stripped
# from the path, and the build path prepended in its place
SERV_OBJECTS = $(SOURCES:%.c=%.server.o)
CLIENT_OBJECTS = $(SOURCES:%.c=%.client.o)

# Main rule, checks the executable and symlinks to the output
all: server client
	@echo "Done"

emulate:
	@make EMULATE=EMULATE

# Removes all build files
clean:
	@rm -f *.o
	@rm -f server
	@rm -f client
	@echo "''' Clean '''"

server: DEFS+=-DSERVER $(A)

# Link the executable
server: $(SERV_OBJECTS)
	@echo "Linking: $@"
ifeq ($(EMULATE),EMULATE)
	@echo "Linking Emulated version"
else
	@echo "Linking Regular version"
endif
	@$(CC) $(SERV_OBJECTS) $(LFLAGS) -o $@

%.server.o: %.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@ $(DEFS)

client: DEFS+=$(A)

# Link the executable
client: $(CLIENT_OBJECTS)
	@echo "Linking: $@"
ifeq ($(EMULATE),EMULATE)
	@echo "Linking Emulated version"
else
	@echo "Linking Regular version"
endif
	@$(CC) $(CLIENT_OBJECTS) $(LFLAGS) -o $@ $(CLIENT_LFLAGS)

%.client.o: %.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@ $(DEFS)
