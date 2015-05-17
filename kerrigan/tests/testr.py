#!/usr/bin/python

import socket
import random
import struct
import string
import time
from Crypto import Random

MAX_TOTAL = 1024 * 16
MAX_PACKET_SIZE = 64
HDR_SIZE = 20

MsgTypes = {0: 'DATA', 1: 'KA', 2: 'FILE', 3: 'PID', 4: 'VARS', 5: 'UPPER'}
pckt_id = [[0, 0, 0, -1]] * 200

OUT_IP = "localhost"
OUT_PORT = 1332
out_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


def set_msg(frag_pck_id, type):
    pckt_id[frag_pck_id][0] = random.randint(0, 0xffffffff)
    pckt_id[frag_pck_id][1] = random.randint(0, MAX_TOTAL)
    pckt_id[frag_pck_id][2] = 0
    pckt_id[frag_pck_id][3] = type


def clear_msg(frag_pck_id):
    pckt_id[frag_pck_id][0] = 0
    pckt_id[frag_pck_id][3] = -1


def ct_rand_data(id_type):
    while True:
        frag_pck_id = random.randint(0, len(pckt_id) - 1)
        if pckt_id[frag_pck_id][3] == -1 or pckt_id[frag_pck_id][3] == id_type:
            break

    if (pckt_id[frag_pck_id][0] == 0):
        set_msg(frag_pck_id, id_type)

    size = random.randint(0, pckt_id[frag_pck_id][1] + 1)
    pckt_id[frag_pck_id][2] += 1
    frag_idx = pckt_id[frag_pck_id][2]

    if (pckt_id[frag_pck_id][2] ==
            int((pckt_id[frag_pck_id][1] + MAX_PACKET_SIZE - 1) /
                MAX_PACKET_SIZE)):
        clear_msg(frag_pck_id)

    data = Random.new().read(8)
    data += struct.pack("<I", size)
    data += struct.pack("<I", pckt_id[frag_pck_id][0])
    data += struct.pack("<I", frag_idx)
    data += struct.pack("<II", id, req)
    data += Random.new().read(MAX_PACKET_SIZE - len(data) + HDR_SIZE)

    return data


def ct_data_packet():
    return ct_rand_data(0)


def ct_ka_packet():
    return ct_rand_data(1)


def ct_file_packet():
    pass


def ct_pid_packet():
    return ct_rand_data(3)


def ct_vars_packet():
    types = ['SHOW', 'SET ', 'DEL ']

    while True:
        frag_pck_id = random.randint(0, len(pckt_id) - 1)
        if pckt_id[frag_pck_id][3] == -1 or pckt_id[frag_pck_id][3] == 4:
            break

    if (pckt_id[frag_pck_id][0] == 0):
        set_msg(frag_pck_id, 4)

    size = random.randint(0, pckt_id[frag_pck_id][1] + 1)
    pckt_id[frag_pck_id][2] += 1
    frag_idx = pckt_id[frag_pck_id][2]

    if (pckt_id[frag_pck_id][2] ==
            int((pckt_id[frag_pck_id][1] + MAX_PACKET_SIZE - 1) /
                MAX_PACKET_SIZE)):
        clear_msg(frag_pck_id)

    data = types[random.randint(0, len(types) - 1)]
    data += ''.join(random.choice(string.ascii_uppercase)
                    for _ in xrange(
                        random.randint(0, MAX_PACKET_SIZE - len(data))))

    if len(data) < MAX_PACKET_SIZE:
        data += ' ' * random.randint(0, MAX_PACKET_SIZE - len(data) - 1)

    ''.join(random.choice(string.ascii_uppercase)
            for _ in xrange(
                random.randint(0, MAX_PACKET_SIZE - len(data))))

    hdr = Random.new().read(8)
    hdr += struct.pack("<I", size)
    hdr += struct.pack("<I", pckt_id[frag_pck_id][0])
    hdr += struct.pack("<I", frag_idx)
    hdr += struct.pack("<II", id, req)

    return hdr + data


def ct_upper_packet():
    while True:
        frag_pck_id = random.randint(0, len(pckt_id) - 1)
        if pckt_id[frag_pck_id][3] == -1 or pckt_id[frag_pck_id][3] == 5:
            break

    if (pckt_id[frag_pck_id][0] == 0):
        set_msg(frag_pck_id, 5)
        path_size = random.randint(0, 350)
        path = ''.join(random.choice(
            string.ascii_letters + string.digits + '/.')
            for _ in xrange(random.randint(0, MAX_PACKET_SIZE - 4)))

        if (len(data))
        data = random


while True:
    id = random.randint(0, len(MsgTypes) - 1)

    if id == 0:
    else:
        size = random.randint(0, MAX_TOTAL)
        frag_idx = int((size + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE)

    req = 0
    print "Sending %s %s" % (MsgTypes[id], ReqType[req])
    data = Random.new().read(8)
    data += struct.pack("<I", size)

    if id == 0:
        data += struct.pack("<I", pckt_id[frag_pck_id][0])
    else:
        data += Random.new().read(4)

    data += struct.pack("<I", frag_idx)
    data += struct.pack("<II", id, req)
    data += Random.new().read(MAX_PACKET_SIZE - len(data) + HDR_SIZE)
    out_sock.sendto(data, (OUT_IP, OUT_PORT))
    time.sleep(0.005)
