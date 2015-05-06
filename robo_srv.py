#!/usr/bin/python
import socket

print "Starting server..."
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
srv_addr = ('localhost', 1334)
sock.bind(srv_addr)

while True:
    data, (ip, port) = sock.recvfrom(4096)
    print "Got %d bytes from %s:%s" % (len(data), ip, port)

    if data:
        print "Echoing back to %s:%s..." % (ip, 1335)
        sock.sendto(data, (ip, 1335))
