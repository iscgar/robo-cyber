#!/usr/bin/python

import hashlib
import itertools
import time
import struct
import binascii
import random
from datetime import datetime
from Crypto.Cipher import AES


class SafeProtocolEncryptor(object):
    _IV = ' ' * AES.block_size
    _CRC_IV = 0x5a634f79
    _EPOCH = datetime(1970, 1, 1)

    def __init__(self, initialKey):
        super(SafeProtocolEncryptor, self).__init__()
        self.key = hashlib.md5(initialKey).digest()
        self.time = self._time_ms()

    def encrypt(self, s):
        self._update_time()
        s = '' if s is None else s
        crypt = AES.new(self._build_key(), AES.MODE_OFB, self._IV)
        padlen = AES.block_size - ((len(s) + 4) % AES.block_size)
        s += padlen * chr(padlen)
        s = struct.pack("<I", binascii.crc32(s, self._CRC_IV) & 0xffffffff) + s
        new_id = self._mangle_id()
        packed_id = struct.pack("<BHI",
                                new_id >> 48,
                                (new_id >> 32) & 0xffff,
                                new_id & 0xffffffff)
        return packed_id + crypt.encrypt(s)

    def _update_time(self):
        new_time = self._time_ms()

        # Sleep for 1 ms
        while (new_time <= self.time):
            time.sleep((self.time - new_time + 1) / 1000)
            new_time = self._time_ms()

        self.time = new_time

    def _build_key(self):
        time = struct.pack("<Q", self.time)
        iter = itertools.izip_longest(self.key, time, fillvalue='\0')
        return ''.join(chr(ord(x) ^ ord(y)) for x, y in iter)

    def _mangle_id(self):
        val = random.randint(0, 95)
        dir = 1

        if val >= 48:
            dir = -1
            val %= 48

        if (val & 3) == 0:
            val = (val + random.randint(0, 3)) % 48

        new_id = self.time

        if dir == 1:
            new_id <<= val
            mask = ((1 << val) - 1) << 48
            new_id |= (new_id & mask) >> 48
        else:
            mask = (1 << val) - 1
            tmp_val = new_id & mask
            new_id = (new_id >> val) | (tmp_val << (48 - val))

        new_id <<= 8
        new_id |= val | (0 if dir == 1 else 0x80)
        new_id &= (1 << 56) - 1

        xor_byte = 0
        result = 0
        for i in xrange(48, -8, -8):
            cur_byte = (new_id >> i) & 0xff
            result <<= 8
            result |= cur_byte ^ xor_byte
            xor_byte ^= cur_byte

        return result & ((1 << 56) - 1)

    def _time_ms(self):
        secs = int((datetime.now() - self._EPOCH).total_seconds() * 1000)
        return secs


import pygame


def implicit_enum(*args):
    return type("ImplicitEnum", (), dict(zip(args, xrange(len(args)))))


def explicit_enum(**args):
    return type("ExplicitEnum", (), args)


class DualShock3(object):
    Analog = explicit_enum(JOY_LEFT_X=0,
                           JOY_LEFT_Y=1,
                           JOY_RIGHT_X=2,
                           JOY_RIGHT_Y=3,
                           B_UP=8,
                           B_RIGHT=9,
                           B_DOWN=10,
                           B_LEFT=11,
                           B_L2=12,
                           B_R2=13,
                           B_L1=14,
                           B_R1=15,
                           B_TRIANGLE=16,
                           B_CIRCLE=17,
                           B_CROSS=18,
                           B_SQUARE=19)

    Digital = explicit_enum(B_SELECT=0,
                            JOY_LEFT=1,
                            JOY_RIGHT=2,
                            B_START=3,
                            B_UP=4,
                            B_RIGHT=5,
                            B_DOWN=6,
                            B_LEFT=7,
                            B_L2=8,
                            B_R2=9,
                            B_L1=10,
                            B_R1=11,
                            B_TRIANGLE=12,
                            B_CIRCLE=13,
                            B_CROSS=14,
                            B_SQUARE=15,
                            B_PS=16)

    analog_axes = dict((value, 0)
                       for (key, value) in Analog.__dict__.iteritems()
                       if not key.startswith('_'))
    digital_buttons = dict((value, 0)
                           for (key, value) in Digital.__dict__.iteritems()
                           if not key.startswith('_'))
    _ps3 = None

    def __init__(self):
        super(DualShock3, self).__init__()

    def init(self):
        if DualShock3._ps3 is None:
            pygame.init()
            pygame.joystick.init()

            joy_count = pygame.joystick.get_count()
            for joy_idx in xrange(joy_count):
                joy = pygame.joystick.Joystick(joy_idx)
                joy.init()

                if "PLAYSTATION(R)3 Controller" in joy.get_name() and \
                        joy.get_numaxes() == 29 and \
                        joy.get_numbuttons() == 17:
                    DualShock3._ps3 = joy
                    break

            if DualShock3._ps3 is None:
                raise Exception("Couldn't find DualShock3 joystick")

    def update(self):
        if DualShock3._ps3 is None:
            return

        pygame.event.get()

        for a in self.analog_axes.iterkeys():
            self.analog_axes[a] = self._ps3.get_axis(a)

        for d in self.digital_buttons.iterkeys():
            self.digital_buttons[d] = self._ps3.get_button(d)

    def cleanup(self):
        pygame.quit()
        DualShock3._ps3 = None


if __name__ == "__main__":
    import sys
    import traceback
    import socket

    ROBO_IP = "localhost"
    ROBO_PORT = 10001
    SPEED_MAX = 255

    Motors = implicit_enum('RIGHT', 'LEFT', 'ARM')

    controller = DualShock3()

    def send_bot_ctrl(sock, crypto, data):
        sock.sendto(crypto.encrypt(data), (ROBO_IP, ROBO_PORT))

    def get_speeds(ctrl):
        arm = 0

        # Set arm motor speed only if a B_SQUARE xor B_CIRCLE is pressed
        if ctrl.digital_buttons[ctrl.Digital.B_SQUARE] != 0 and \
                ctrl.digital_buttons[ctrl.Digital.B_CIRCLE] == 0:
            arm = SPEED_MAX
        elif ctrl.digital_buttons[ctrl.Digital.B_CIRCLE] != 0 and \
                ctrl.digital_buttons[ctrl.Digital.B_SQUARE] == 0:
            arm = -SPEED_MAX

        forward = ctrl.analog_axes[ctrl.Analog.B_R2]
        back = ctrl.analog_axes[ctrl.Analog.B_L2]

        # Normalize mutual press
        wheels_speed = (forward - back) * SPEED_MAX
        right = left = wheels_speed

        # Ignore turn values when not in movement
        if wheels_speed != 0:
            # Get turn parameters from controller
            turn = ctrl.analog_axes[ctrl.Analog.JOY_RIGHT_X]
            abs_turn = abs(turn)
            in_place_turn = ctrl.digital_buttons[ctrl.Digital.B_UP] | \
                ctrl.digital_buttons[ctrl.Digital.B_RIGHT] | \
                ctrl.digital_buttons[ctrl.Digital.B_DOWN] | \
                ctrl.digital_buttons[ctrl.Digital.B_LEFT]

            # Calculate wheels turn
            if (abs_turn > 0.1):
                if (turn > 0):
                    right = right - abs_turn * left if in_place_turn == 0 \
                        else abs_turn * (-left)
                else:
                    left = left - abs_turn * right if in_place_turn == 0 \
                        else abs_turn * (-right)

        return right, left, arm

    try:
        controller.init()

        crypto = SafeProtocolEncryptor("I am Johnny Six")
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        while True:
            controller.update()

            right, left, arm = get_speeds(controller)

            send_bot_ctrl(sock, crypto, struct.pack("<Hi", Motors.RIGHT, right))
            send_bot_ctrl(sock, crypto, struct.pack("<Hi", Motors.LEFT, left))
            send_bot_ctrl(sock, crypto, struct.pack("<Hi", Motors.ARM, arm))

            pygame.time.wait(50)
    except KeyboardInterrupt:
        pass
    except Exception:
        traceback.print_exc(file=sys.stdout)

    controller.cleanup()
