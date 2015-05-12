#!/usr/bin/python
import socket
import time

print "Starting controller..."
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
srv_addr = ('localhost', 1330)
sock.bind(srv_addr)
rnd = open("/dev/urandom", "r")

i = 0
while True:
    time.sleep(1)

    i = (i + 1) % 7
