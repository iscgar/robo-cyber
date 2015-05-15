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
        packed_id = struct.pack("<BHI", new_id >> 48, (new_id >> 32) & 0xffff, new_id & 0xffffffff)
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
        epoch = datetime(1970, 1, 1)
        secs = int((datetime.now() - epoch).total_seconds() * 1000)
        return secs


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

import string


if __name__ == "__main__":
    e = SafeProtocolEncryptor("Jonny Six is the greatest robot of all")
    d = SafeProtocolDecryptor("Jonny Six is the greatest robot of all")
    rndstr = ''.join(random.choice(string.ascii_lowercase + string.digits) for _ in xrange(random.randint(1000, 100000)))
    print "p%d: %s" % (len(rndstr), rndstr)

    x = 0
    for i in xrange(100):
        data = e.encrypt(rndstr)
        dec = d.decrypt(data)
        if (dec != rndstr):
            print "ERROR: %d" % len(dec)
            x += 1

    print "%d decryption errors" % x
