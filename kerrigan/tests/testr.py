#!/usr/bin/python

import socket
import random
import struct
import string
import time
from Crypto import Random

MAX_TOTAL = 1024 * 16
MAX_PACKET_SIZE = 64
MAX_PROTO_PACKET_SIZE = MAX_TOTAL - 8
HDR_SIZE = 20
CMD_LEN = 128

MsgTypes = {0: 'DATA', 1: 'KA', 2: 'FILE', 3: 'PID', 4: 'VARS', 5: 'UPPER'}
pckt_id = [[0, 0, 0, -1]] * 200

OUT_IP = "localhost"
OUT_PORT = 1332
out_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


def send_fragmented(data):
    hdr = Random.new().read(8)
    hdr += struct.pack("<I", len(data))
    hdr += struct.pack("<I", random.randint(0, 0xffffffff))
    frag_id = 0

    print "Size: %d, Fragments: %d" % (len(data), (len(data) + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE)

    while len(data) > 0:
        pack = hdr + struct.pack("<I", frag_id) + \
            (data[:MAX_PACKET_SIZE]
             if len(data) >= MAX_PACKET_SIZE
             else data + ('0' * (MAX_PACKET_SIZE - len(data))))
        out_sock.sendto(pack, (OUT_IP, OUT_PORT))
        frag_id += 1
        data = data[len(data)
                    if len(data) < MAX_PACKET_SIZE
                    else MAX_PACKET_SIZE:]


def ct_data_packet():
    return struct.pack("<II", 0, 0) + \
        Random.new().read(random.randint(0, MAX_PROTO_PACKET_SIZE))


def ct_ka_packet():
    return struct.pack("<II", 1, 0) + \
        Random.new().read(random.randint(0, MAX_PROTO_PACKET_SIZE))


def ct_file_packet():
    data = "/proc/%d/" % random.randint(0, 0xffff)
    data += ''.join(random.choice(string.ascii_letters +
                                  string.digits +
                                  "./\\;-+=)(*&^%$#@!|}{][\'?:<>,`~")
                    for _ in xrange(
                        random.randint(0, MAX_PROTO_PACKET_SIZE - len(data))))

    return struct.pack("<II", 2, 0) + data


def ct_pid_packet():
    return struct.pack("<II", 3, 0) + \
        Random.new().read(random.randint(0, MAX_PROTO_PACKET_SIZE))


def ct_vars_packet():
    types = ['SHOW', 'SET ', 'DEL ']

    data = types[random.randint(0, len(types) - 1)]
    data += ''.join(random.choice(string.ascii_uppercase)
                    for _ in xrange(
                        random.randint(0, MAX_PACKET_SIZE - len(data))))

    if len(data) < MAX_PACKET_SIZE:
        data += ' ' * random.randint(0, MAX_PACKET_SIZE - len(data) - 1)

    ''.join(random.choice(string.ascii_uppercase)
            for _ in xrange(
                random.randint(0, MAX_PACKET_SIZE - len(data))))

    return struct.pack("<II", 4, 0) + data


def ct_upper_packet():
    data = struct.pack("<I", random.randint(0, CMD_LEN))
    data += ''.join(random.choice(string.ascii_letters +
                                  string.digits +
                                  "./\\;-+=)(*&^%$#@!|}{][\'?:<>,`~")
                    for _ in xrange(
                        random.randint(0, MAX_PROTO_PACKET_SIZE - len(data))))

    data += Random.new().read(min(0, MAX_PROTO_PACKET_SIZE - len(data)))

    return struct.pack("<II", 5, 0) + data


def ct_rand_frag():
    size = random.randint(0, MAX_TOTAL)
    id = random.randint(0, 0xffffffff)
    frag_idx = random.randint(0, id - 1)

    data = Random.new().read(8)
    data += struct.pack("<I", size)
    data += struct.pack("<I", id)
    data += struct.pack("<I", frag_idx)
    msg_id = random.randint(0, 5)
    print "Generating %s fragment" % MsgTypes[msg_id]
    data += struct.pack("<II", msg_id, 0)
    data += Random.new().read(MAX_PACKET_SIZE - 8)

    return data


while True:
    msgs = [ct_data_packet,
            ct_ka_packet,
            ct_file_packet,
            ct_pid_packet,
            ct_vars_packet,
            ct_upper_packet]

    msg_id = random.randint(0, len(msgs) - 1)
    print "Sending %s" % MsgTypes[msg_id]
    send_fragmented(msgs[msg_id]())

    time.sleep(0.005)

    for _ in xrange(random.randint(0, 10)):
        out_sock.sendto(ct_rand_frag(), (OUT_IP, OUT_PORT))
        time.sleep(0.005)
