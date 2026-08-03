#!/usr/bin/env python3
"""Minimal NTP server (RFC 5905 subset) for RmikuOS ntpdate testing.

Run on the HOST. QEMU slirp user-mode networking forwards guest traffic
to 10.0.2.2:<port> onto the host's loopback, so the RmikuOS guest reaches
this server at 10.0.2.2:<port> with a plain UDP socket.

Usage:
    sudo python3 tools/ntp_server.py            # port 123 (standard, needs root)
    python3 tools/ntp_server.py -p 12300        # any unprivileged port

Response implements the fields the client needs:
  - 4 timestamps (Origin echoed, Receive/Transmit = server clock)
  - 64-bit NTP timestamps (secs since 1900, 32-bit fraction)
  - server mode (Mode=4), VN=4
Leap/second/rollover are intentionally ignored (teaching subset).
"""

import argparse
import socket
import struct
import time

# Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01).
NTP_EPOCH_OFFSET = 2208988800
MODE_SERVER = 4
VN = 4


def to_ntp_64(t: float) -> bytes:
    """Convert a Unix epoch (float seconds) to an NTP 64-bit timestamp."""
    secs = int(t) + NTP_EPOCH_OFFSET
    frac = int((t - int(t)) * (1 << 32))
    return struct.pack(">II", secs & 0xFFFFFFFF, frac)


def build_response(req: bytes, t2: float, t3: float) -> bytes:
    """Build a 48-byte NTP response from a (48-byte) request."""
    if len(req) < 48:
        req = req.ljust(48, b"\0")
    origin = req[24:32]  # Origin timestamp (T1) is echoed back unchanged

    li_vn_mode = (0 << 6) | (VN << 3) | MODE_SERVER  # 0x24: LI=0 VN=4 Mode=4
    resp = struct.pack(
        ">BBBBII",
        li_vn_mode,
        2,              # stratum: secondary server (sourced from local clock)
        6,              # poll interval: 64 s
        (-22) & 0xFF,   # precision: ~2^-22 s (~0.24 us)
        0,              # root delay (32-bit fixed point, seconds << 16)
        0,              # root dispersion
    )
    resp += b"\0\0\0\0"       # reference ID (unspecified)
    resp += to_ntp_64(t2)     # reference timestamp (when clock was set)
    resp += origin            # origin timestamp = request's T1
    resp += to_ntp_64(t2)     # receive timestamp T2 (server clock at arrival)
    resp += to_ntp_64(t3)     # transmit timestamp T3 (server clock at send)
    return resp[:48]


def main() -> None:
    ap = argparse.ArgumentParser(description="Minimal NTP server for RmikuOS")
    ap.add_argument(
        "-p", "--port", type=int, default=123,
        help="UDP port to listen on (default 123; <1024 usually needs root)",
    )
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    print(f"[ntpd] listening on 0.0.0.0:{args.port} "
          f"(guest reaches it at 10.0.2.2:{args.port})", flush=True)

    while True:
        data, addr = sock.recvfrom(512)
        t2 = time.time()
        resp = build_response(data, t2, time.time())
        sock.sendto(resp, addr)
        print(f"[ntpd] replied to {addr} ({len(data)}B)", flush=True)


if __name__ == "__main__":
    main()
