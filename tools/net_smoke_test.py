#!/usr/bin/env python3
"""Room-server protocol smoke test.

Usage:  ./build/opera_server 7790 &   then   python3 tools/net_smoke_test.py 7790

Checks join/welcome, split-packet reassembly, late-join state (pose, rig, rec flag),
note relay with sender ids, abrupt disconnects and the Leave broadcast.
Run it against a throwaway server: it uploads an empty test lighting rig, which the
server then hands to anyone who joins later. Restart the server before playing.
"""
import socket, struct, time, sys
PORT = int(sys.argv[1])
HDR = struct.Struct('<BII')  # type, size, playerId (packed, 9 bytes)
def pkt(t, payload=b''): return HDR.pack(t, HDR.size + len(payload), 0) + payload
def conn():
    s = socket.create_connection(('127.0.0.1', PORT)); s.settimeout(1.0); return s
def recv_all(s, wait=0.4):
    buf = b''; end = time.time() + wait
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d: break
            buf += d
        except socket.timeout: break
    out = []
    while len(buf) >= HDR.size:
        t, size, pid = HDR.unpack_from(buf)
        if len(buf) < size: break
        out.append((t, pid, buf[HDR.size:size])); buf = buf[size:]
    return out
# PlayerState: 4f, B role, b seat, B sit, B playing, f mouth, B inst, B suit, B inHouse, 24s name
def pose(x, name): return struct.pack('<4fBbBBfBBB24s', x, 1.1, -5, 0, 1, -1, 0, 0, 0.5, 0, 0, 1, name)
A = conn()
# split a Join across two sends to exercise stream reassembly
j = pkt(1, b'Alice'); A.sendall(j[:4]); time.sleep(0.1); A.sendall(j[4:])
w = recv_all(A); assert w and w[0][0] == 2, w; aid = struct.unpack('<I', w[0][2])[0]; print('A welcome id', aid)
A.sendall(pkt(4, pose(-2.2, b'Alice')) + pkt(6, b'OGRIG 2\n0.5 0.2 0.3 0\n') + pkt(7, b'\x01'))
time.sleep(0.2)
B = conn(); B.sendall(pkt(1, b'Bob'))
msgs = recv_all(B); types = [m[0] for m in msgs]
print('B got on join:', types)
assert 2 in types and 4 in types and 6 in types and 7 in types, 'late join missing state'
bid = struct.unpack('<I', msgs[0][2])[0]
# note relay A -> B, sent byte-by-byte
n = pkt(5, bytes([0, 60, 100, 1, 0xff]))
for b in n: A.send(bytes([b]))
got = recv_all(B)
assert any(m[0] == 5 and m[1] == aid for m in got), got; print('note relayed with sender id', aid)
# abrupt disconnect of B while A floods; server must survive and announce Leave
B.close()
for i in range(200): A.sendall(pkt(4, pose(float(i), b'Alice')))
got = recv_all(A, 0.6)
assert any(m[0] == 3 and m[1] == bid for m in got), [m[0] for m in got]; print('leave broadcast for', bid)
C = conn(); C.sendall(pkt(1, b'Carol')); got = recv_all(C); print('server alive after abrupt close; Carol got', [m[0] for m in got])
print('ALL NET TESTS PASSED')
