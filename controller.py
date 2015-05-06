#!/usr/bin/python
import socket
import time

print "Starting controller..."
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
srv_addr = ('localhost', 1330)
sock.bind(srv_addr)

i = 0
while True:
    time.sleep(1)
    sent_data = "Hello " + str(i)
    dest_addr = ('localhost', 1331)

    print "Sending \'%s\' to %s..." % (sent_data, dest_addr)
    sock.sendto(sent_data, dest_addr)

    data, addr = sock.recvfrom(4096)
    print "Got %d bytes from  %s" % (len(data), addr)

    if data:
        print "Data recived is: \'%s\'" % data

    i += 1
