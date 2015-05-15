#!/usr/bin/python

import socket
from BrickPi import *

# Initialize brickpi motors
BrickPiSetup()
BrickPi.MotorEnable[PORT_A] = 1
BrickPi.MotorEnable[PORT_B] = 1
BrickPi.MotorEnable[PORT_C] = 1

# Networking consts
LISTEN_IP = "localhost"
LISTEN_PORT = 10003
UDP_SOCK = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
UDP_SOCK.bind(("0.0.0.0", LISTEN_PORT))

def GetMessage():
  data, addr = UDP_SOCK.recvfrom(1024)
  HandleMessage(data)
  print "Client: I received your data:", data

def HandleMessage(data):
  data = data.split(",")
  motor = data[0]
  speed = int(data[1]) * (-1)
  MotorSpeed = None
  print motor, speed
  if motor == 'a':
    BrickPi.MotorSpeed[PORT_A] = speed
  if motor == 'b':
    BrickPi.MotorSpeed[PORT_B] = speed
  if motor == 'c':
    BrickPi.MotorSpeed[PORT_C] = speed
  if motor == 'd':
    BrickPi.MotorSpeed[PORT_D] = speed

  BrickPiUpdateValues()

print "Client: Started..."
# Handle packets forever...
while True:
  GetMessage()
print "Client: Dead..."

