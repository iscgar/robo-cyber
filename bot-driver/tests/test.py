import socket
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

while True:
	print "Sending hello..."
	sock.sendto("Hello!", ("localhost", 10003))
	time.sleep(1)
