#!/usr/bin/python

import pygame
import socket

# Color consts
BACKGROUND_COLOR = (255, 255, 255) # WHITE
TEXT_COLOR = (0, 0, 50) # DARK BLUE

# Networking consts
SERVER_IP = "localhost"
SERVER_PORT = 10001
UDP_SOCK = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def MessageBot(data):
  print("Sending packet: " + data)
  UDP_SOCK.sendto(data, (SERVER_IP, SERVER_PORT))

# This is a simple class that will help us print to the screen
# It has nothing to do with the joysticks, just outputing the
# information.
class TextPrint:
    def __init__(self):
        self.reset()
        self.font = pygame.font.Font(None, 20)

    def printt(self, screen, textString):
        textBitmap = self.font.render(textString, True, TEXT_COLOR)
        screen.blit(textBitmap, [self.x, self.y])
        self.y += self.line_height
        
    def reset(self):
        self.x = 10
        self.y = 10
        self.line_height = 15
        
    def indent(self):
        self.x += 10
        
    def unindent(self):
        self.x -= 10
    

pygame.init()

# Set the width and height of the screen [width,height]
size = [500, 300]
screen = pygame.display.set_mode(size)

pygame.display.set_caption("Garbage Collector Control Screen")

#Loop until the user clicks the close button.
done = False

# Used to manage how fast the screen updates
clock = pygame.time.Clock()

# Initialize the joysticks
pygame.joystick.init()
    
# Get ready to print
textPrint = TextPrint()

def HandleAxes(axL2, axR2, axLS, axRS, axSquare, axCircle):

  axRL = axR2 - axL2

  engSpeedB = engSpeedC = axRL * 250
  engSpeedA = 0
    
  if(axSquare > 0.1):
    engSpeedA = 200
  if(axCircle > 0.1):
    engSpeedA = -200
  

  if (axLS != 0 and (axRS + 0.2 < 0.4)):
    if axLS > 0:
      engSpeedC *= (-2 * axLS) + 1
    elif axLS < 0:
      engSpeedB *= 2 * axLS + 1
  elif (axRS != 0 and (axLS + 0.2 < 0.4)):
    if axRS < 0:
      engSpeedB *= (1 - (axRS * (-1)))
    elif axRS > 0:
      engSpeedC *= (1 - axRS)
  else:
    pass

  MessageBot("b,{}".format(int(engSpeedA)))
  MessageBot("a,{}".format(int(engSpeedB)))
  MessageBot("c,{}".format(int(engSpeedC)))
  
# -------- Main Program Loop -----------
while done == False:
    # EVENT PROCESSING STEP
    for event in pygame.event.get(): # User did something
        if event.type == pygame.QUIT: # If user clicked close
            done=True # Flag that we are done so we exit this loop
 
    # DRAWING STEP
    # First, clear the screen with the background color. Don't put other drawing commands
    # above this, or they will be erased with this command.
    screen.fill(BACKGROUND_COLOR)
    textPrint.reset()

    # Get count of joysticks
    joystick_count = pygame.joystick.get_count()

    if joystick_count == 0:
      print("I can't find any joysticks.")
      break

    print("Number of joysticks: {}".format(joystick_count))
  
    dualshock = None

    # Look for DualShock3
    for currentId in range(joystick_count):
      joystick = pygame.joystick.Joystick(currentId)
      joystick.init()
      name = joystick.get_name()
      print("Found joystick: {}".format(name))
      if "PLAYSTATION(R)3 Controller" in name:
        dualshock = joystick

    # if no dualshock3 joystick was found, break.
    if dualshock == None:
      print("None of the joysticks are dualshock3 controllers")
      break

    textPrint.printt(screen, "Found Dualschok3 controller!")

    # axes
    axes = joystick.get_numaxes()
    axL2 = 0
    axR2 = 0
    axLS = 0
    axRS = 0
    axSquare = 0
    axCircle = 0
    for i in range(axes):
        axis = joystick.get_axis( i )
        if i == 2:
          axRS = axis
        if i == 0:
          axLS = axis
        if i == 12:
          axL2 = axis
        if i == 13:
          axR2 = axis
        if i == 19:
          axSquare = axis
        if i == 17:
          axCircle = axis
    
    HandleAxes(axL2, axR2, axLS, axRS, axSquare, axCircle)

    # Go ahead and update the screen with what we've drawn.
    pygame.display.flip()

    # Limit to 20 frames per second
    clock.tick(20)
    
# Close the window and quit.
# If you forget this line, the program will 'hang'
# on exit if running from IDLE.
pygame.quit()

