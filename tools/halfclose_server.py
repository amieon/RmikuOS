#!/usr/bin/env python3
# halfclose_server.py —— halfclose.c 的宿主机对端
#
# 行为:accept -> 读请求直到对端 FIN(即 guest 的 SHUT_WR) -> 把请求原样
# 回显并追加统计尾注 -> close(发 FIN)。
# 两个方向的数据长度都事先未知,完全靠 FIN 界定结束——正是半关闭的场景。
#
# 用法: python3 halfclose_server.py [port]     默认 9999
# guest 侧经 slirp 访问 10.0.2.2:9999 会自动转发到宿主机 loopback,无需 hostfwd。

import socket, sys

port = int(sys.argv[1]) if len(sys.argv) > 1 else 9999

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", port))
srv.listen(4)
print(f"[server] listening on 127.0.0.1:{port}, waiting for RmikuOS ...")

while True:
    conn, peer = srv.accept()
    print(f"[server] conn from {peer}")

    # ① 读请求直到 EOF —— 对端 shutdown(SHUT_WR) 的 FIN 在这里变成 b""
    req = b""
    while True:
        chunk = conn.recv(4096)
        if not chunk:
            break
        req += chunk
    print(f"[server] request EOF (peer half-closed), got {len(req)} bytes:")
    for line in req.decode(errors="replace").splitlines():
        print(f"  | {line}")

    # ② 回显 + 统计尾注,长度同样不预告
    tail = f"[server] echoed {len(req)} bytes, bye from host\n"
    conn.sendall(req + tail.encode())

    # ③ close -> FIN 飞向 guest,成为那边的 EOF
    conn.close()
    print("[server] response sent, closed (FIN)\n")