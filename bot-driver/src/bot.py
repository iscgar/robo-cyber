#!/usr/bin/python

import hashlib
import itertools
import struct
import binascii
from Crypto.Cipher import AES


class SafeProtocolDecryptor(object):
    _IV = ' ' * AES.block_size
    _CRC_IV = 0x5a634f79

    def __init__(self, initialKey):
        super(SafeProtocolDecryptor, self).__init__()
        self.key = hashlib.md5(initialKey).digest()
        self.last_id = 0

    def decrypt(self, s):
        # ID + CRC at least!
        if s is None or len(s) < 11 or (len(s) - 7) % 16 != 0:
            return ''

        id_v = struct.unpack("<BHI", s[:7])
        new_id = (id_v[0] << 48) | (id_v[1] << 32) | id_v[2]
        new_id = self._demangle_id(new_id)

        if (new_id <= self.last_id):
            return ''

        s = s[7:]
        crypt = AES.new(self._build_key(new_id), AES.MODE_OFB, self._IV)
        s = crypt.decrypt(s)

        crc = struct.unpack("<I", s[:4])[0]
        s = s[4:]

        if (binascii.crc32(s, self._CRC_IV) & 0xffffffff) != crc:
            return ''

        s = s[:-ord(s[len(s)-1:])]
        self.last_id = new_id

        return s

    def _build_key(self, id):
        time = struct.pack("<Q", id)
        iter = itertools.izip_longest(self.key, time, fillvalue='\0')
        return ''.join(chr(ord(x) ^ ord(y)) for x, y in iter)

    def _demangle_id(self, id):
        result = 0
        xor_byte = 0
        for i in xrange(48, -8, -8):
            cur_byte = (id >> i) & 0xff ^ xor_byte
            xor_byte ^= cur_byte
            result <<= 8
            result |= cur_byte

        val = result & 0x7f
        dir = -1 if (result & 0x80) == 0 else 1
        result >>= 8

        if dir == 1:
            result <<= val
            mask = ((1 << val) - 1) << 48
            result |= (result & mask) >> 48
        else:
            mask = (1 << val) - 1
            tmp_val = result & mask
            result = (result >> val) | (tmp_val << (48 - val))

        result &= (1 << 48) - 1

        return result


def implicit_enum(*args):
    return type("ImplicitEnum", (), dict(zip(args, xrange(len(args)))))


def explicit_enum(**args):
    return type("ExplicitEnum", (), args)


if __name__ == "__main__":
    import socket
    from datetime import datetime, timedelta
    from BrickPi import *

    SPEED_MAX = 255
    MOTOR_TIMEOUT = 0.1

    Motors = implicit_enum('RIGHT', 'LEFT', 'ARM')
    ports = {Motors.RIGHT: PORT_A, Motors.LEFT: PORT_C, Motors.ARM: PORT_B}
    #ports = {Motors.RIGHT: 0, Motors.LEFT: 2, Motors.ARM: 1}

    # Initialize brickpi motors
    BrickPiSetup()

    for port in ports.values():
        #print "Enabling port %d..." % port
        BrickPi.MotorEnable[port] = 1

    # Initialize decryption engine
    crypto = SafeProtocolDecryptor("I am Johnny Six")

    # Networking consts
    LISTEN_PORT = 10043
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind(("0.0.0.0", LISTEN_PORT))
    udp_sock.settimeout(MOTOR_TIMEOUT)

    # Motors timeouts list
    timeout_list = dict((key, None) for key in ports.iterkeys())

    def set_speed(motor, speed):
        port = ports[motor]

        BrickPi.MotorSpeed[port] = speed
        BrickPiUpdateValues()
        #print "Motor %d:%d set to %d" % (motor, ports[motor], speed)

    def handle_message(sock, dec):
        try:
            data = sock.recv(1024)
        except socket.timeout:
            return

        msg = dec.decrypt(data)

        if msg is None or len(msg) != 6:
            return

        parsed = struct.unpack("<Hi", msg)
        motor = parsed[0]

        if (motor > Motors.ARM):
            return

        speed = parsed[1]

        if (abs(speed) > SPEED_MAX):
            return

        set_speed(motor, speed)

    print "Client: Started..."

    # Handle packets forever...
    while True:
        handle_message(udp_sock, crypto)
