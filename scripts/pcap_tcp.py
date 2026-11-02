#!/usr/bin/env python3
"""把 QEMU filter-dump 出来的 pcap 打成一条 TCP 时间线。

存在的理由：那个文件里 99% 是 DNS 和 ARP，用 Wireshark 点开要过滤半天，而排查
"请求到底发出去没有 / 服务端到底回了没有" 只需要一列带 seq/len 的行。这个脚本
只依赖标准库，不装任何东西就能跑。

    python3 scripts/pcap_tcp.py /tmp/rmiku.pcap --port 443
    python3 scripts/pcap_tcp.py /tmp/rmiku.pcap --port 443 --data 48
    python3 scripts/pcap_tcp.py /tmp/rmiku.pcap --port 443 --summary-only

输出分三块：
  1. 会话概览   —— 谁跟谁说了多少帧，先确认 443 的流量到底在不在
  2. 时间线     —— 每个 TCP 段一行：方向 / 标志 / 相对 seq / payload 长度
  3. 统计       —— 每方向段数+字节数、重传、序号空洞（丢包）

★ 关键判据在最后一块：out_bytes 是 guest 发出去的 payload，in_bytes 是服务端发回来的。
   in_bytes=0  → 服务端一个数据字节都没给（它不认我们的请求 / 或者根本没收到）
   in_bytes 上千 → 数据到了我们网卡，问题在交付给应用那一段
"""

import argparse
import struct
import sys
from collections import Counter, OrderedDict

# pcap 全局头的魔数（含纳秒精度变体）。pcapng 是 0a0d0d0a，QEMU 不会写。
MAGIC = {
    b"\xd4\xc3\xb2\xa1": ("<", 1000000),
    b"\xa1\xb2\xc3\xd4": (">", 1000000),
    b"\x4d\x3c\xb2\xa1": ("<", 1000000000),
    b"\xa1\xb2\x3c\x4d": (">", 1000000000),
}

FLAG_NAMES = (
    (0x01, "F"), (0x02, "S"), (0x04, "R"), (0x08, "P"),
    (0x10, "."), (0x20, "U"), (0x40, "E"), (0x80, "C"),
)

# TLS 记录层 content type。标签要短，一条 TCP 段里可能塞着好几条记录。
TLS_TYPES = {
    0x14: "CCS", 0x15: "alert", 0x16: "hs", 0x17: "app", 0x18: "hb",
}


def die(msg):
    sys.stderr.write("pcap_tcp: %s\n" % msg)
    sys.exit(2)


def read_pcap(path):
    """返回 (linktype, [ (ts, raw_bytes), ... ])。"""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        die("打不开 %s: %s" % (path, e))

    if len(data) < 4:
        die("%s 只有 %d 字节，不像 pcap（QEMU 是否已经启动过？）" % (path, len(data)))

    magic = data[:4]
    if magic == b"\x0a\x0d\x0d\x0a":
        die("这是 pcapng，不是 pcap。先转一下： tshark -r in.pcapng -w out.pcap -F pcap")
    if magic not in MAGIC:
        die("魔数 %s 不认识，不是 pcap 文件" % magic.hex())
    if len(data) < 24:
        die("%s 只有 %d 字节，全局头都不全（filter-dump 还没写过任何包）" % (path, len(data)))

    endian, ticks = MAGIC[magic]
    snaplen, linktype = struct.unpack(endian + "II", data[16:24])

    frames = []
    off = 24
    n = len(data)
    while off + 16 <= n:
        ts_sec, ts_frac, incl, _orig = struct.unpack(endian + "IIII", data[off:off + 16])
        off += 16
        if incl > n - off:
            break  # 抓包被 kill 时最后一条常常是截断的，丢掉即可
        frames.append((ts_sec + ts_frac / ticks, data[off:off + incl]))
        off += incl
    return linktype, snaplen, frames


def strip_link_layer(linktype, b):
    """剥掉链路层，返回 (ethertype, body)；不支持就返回 (None, None)。"""
    if linktype == 1:  # EN10MB
        if len(b) < 14:
            return None, None
        et = struct.unpack("!H", b[12:14])[0]
        off = 14
        while et in (0x8100, 0x88A8, 0x9100):  # VLAN 标签
            if len(b) < off + 4:
                return None, None
            et = struct.unpack("!H", b[off + 2:off + 4])[0]
            off += 4
        return et, b[off:]
    if linktype == 113:  # LINUX_SLL
        if len(b) < 16:
            return None, None
        return struct.unpack("!H", b[14:16])[0], b[16:]
    if linktype in (0, 108):  # BSD loopback / null，4 字节地址族
        if len(b) < 4:
            return None, None
        return 0x0800, b[4:]
    return None, None


def parse_ipv4(b):
    if len(b) < 20:
        return None
    ver_ihl = b[0]
    if ver_ihl >> 4 != 4:
        return None
    ihl = (ver_ihl & 0x0F) * 4
    if ihl < 20 or len(b) < ihl:
        return None
    total = struct.unpack("!H", b[2:4])[0]
    frag = struct.unpack("!H", b[6:8])[0]
    proto = b[9]
    src = ".".join(str(x) for x in b[12:16])
    dst = ".".join(str(x) for x in b[16:20])
    body = b[ihl:total] if 0 < total <= len(b) else b[ihl:]
    return {
        "src": src, "dst": dst, "proto": proto, "body": body,
        "frag_off": frag & 0x1FFF, "mf": bool(frag & 0x2000),
    }


def parse_tcp(b):
    if len(b) < 20:
        return None
    sport, dport, seq, ack, off_flags = struct.unpack("!HHIIH", b[:14])
    doff = (off_flags >> 12) * 4
    if doff < 20 or len(b) < doff:
        return None
    flags = off_flags & 0x3F
    # 只取低 6 位会漏 ECN 位，但这两位对排查无意义
    win = struct.unpack("!H", b[14:16])[0]
    return {
        "sport": sport, "dport": dport, "seq": seq, "ack": ack,
        "flags": flags, "win": win, "payload": b[doff:],
    }


def flags_str(f):
    return "".join(n for bit, n in FLAG_NAMES if f & bit) or "-"


def build_streams(segs):
    """按四元组把 payload 重组成字节流，并记下每段在流里的偏移。

    **必须重组**：TLS 记录可以跨多个 TCP 段，一条 4KB 的 Certificate 记录会被
    切成 1440+1440+1117。前两版都是按单段解析记录的，于是把完全正常的握手
    flight 报成 `[hs+CCS+app!截断(需3864有1307)]` —— 连报两次假警。记录边界
    只有看整条流才对得上。
    """
    streams = {}
    for idx, s in enumerate(segs):
        key = (s["src"], s["sport"], s["dst"], s["dport"])
        st = streams.setdefault(key, {"isn": s["seq"], "base": None, "off": {}})
        # 流偏移要相对**第一个带 payload 的段**，不能相对 SYN 的 ISN：SYN 占一个
        # 序号但不带数据，以它为基准的话流首字节是空洞，记录解析连第一个字节都
        # 对不上（buf[0]=0 → 判成"不是 TLS"）。
        if s["payload"] and st["base"] is None:
            st["base"] = s["seq"]
        st["off"][idx] = None

    for key, st in streams.items():
        if st["base"] is None:
            st["buf"], st["have"] = bytearray(), bytearray()
            continue
        total = 0
        for idx, s in enumerate(segs):
            if (s["src"], s["sport"], s["dst"], s["dport"]) != key or not s["payload"]:
                continue
            off = (s["seq"] - st["base"]) & 0xFFFFFFFF
            if off > 0x7FFFFFFF:        # 落在基准之前（回绕错觉），丢开
                continue
            st["off"][idx] = off
            total = max(total, off + len(s["payload"]))
        buf = bytearray(total)
        have = bytearray(total)
        for idx, off in st["off"].items():
            if off is None:
                continue
            p = segs[idx]["payload"]
            buf[off:off + len(p)] = p
            have[off:off + len(p)] = b"\x01" * len(p)
        st["buf"], st["have"] = buf, have
    return streams


def parse_records(buf, have):
    """在整条流上切 TLS 记录，返回 [(start, end, name)]；不是 TLS 返回 None。"""
    if len(buf) < 5 or buf[0] not in TLS_TYPES:
        return None
    recs = []
    off = 0
    while off + 5 <= len(buf):
        ct = buf[off]
        if ct not in TLS_TYPES:
            recs.append((off, len(buf), "非记录"))
            break
        rlen = struct.unpack("!H", bytes(buf[off + 3:off + 5]))[0]
        end = off + 5 + rlen
        name = TLS_TYPES[ct]
        if end > len(buf):
            recs.append((off, len(buf), name + "?未完"))
            break
        if 0 in have[off:end]:
            name += "?缺"
        recs.append((off, end, name))
        off = end
        if len(recs) >= 40:
            break
    return recs


def tag_for(off, length, records):
    """给一段 payload 打标签：只看"记录起点落在本段内"的记录，跨段的标 (未完)/续。"""
    if records is None or not length:
        return ""
    end = off + length
    parts = [name + ("(未完)" if re_ > end else "")
             for rs, re_, name in records if off <= rs < end]
    tag = "+".join(parts[:4])
    if any(rs < off < re_ for rs, re_, _ in records):
        tag = ("续+" + tag) if tag else "续"
    return tag


def hexdump(payload, limit):
    payload = payload[:limit]
    groups = []
    for i in range(0, len(payload), 8):
        groups.append(" ".join("%02x" % c for c in payload[i:i + 8]))
    ascii_part = "".join(chr(c) if 32 <= c < 127 else "." for c in payload)
    return "   ".join(groups) + "   |" + ascii_part + "|"


def main():
    ap = argparse.ArgumentParser(description="TCP 时间线（纯标准库 pcap 解析）")
    ap.add_argument("pcap")
    ap.add_argument("--port", type=int, default=None, help="只看这个 TCP 端口（任一侧匹配）")
    ap.add_argument("--ip", default=None, help="只看这个 IP（任一侧匹配）")
    ap.add_argument("--data", type=int, default=0,
                    help="payload 非空时打印前 N 字节（hex+ascii），默认 0 = 不打")
    ap.add_argument("--max", type=int, default=400, help="时间线最多打印多少段，默认 400")
    ap.add_argument("--summary-only", action="store_true", help="只打统计，不打时间线")
    args = ap.parse_args()

    linktype, snaplen, frames = read_pcap(args.pcap)
    if not frames:
        die("抓包里一个包都没有（QEMU 启动后 filter-dump 才开始写，跑一次 curl 再看）")

    t0 = frames[0][0]
    t_end = frames[-1][0]

    conv = Counter()          # (src:port -> dst:port) 帧数
    ip_proto = Counter()      # 协议分布
    tcp_segs = []             # 过滤后的 TCP 段
    truncated = 0

    for ts, raw in frames:
        et, body = strip_link_layer(linktype, raw)
        if et != 0x0800:
            if et == 0x0806:
                ip_proto["ARP"] += 1
            elif et == 0x86DD:
                ip_proto["IPv6"] += 1
            else:
                ip_proto["其他(0x%04x)" % (et or 0)] += 1
            continue
        ip = parse_ipv4(body)
        if ip is None:
            continue
        if ip["proto"] == 17:
            ip_proto["UDP"] += 1
            continue
        if ip["proto"] != 6:
            ip_proto["IP-proto-%d" % ip["proto"]] += 1
            continue
        ip_proto["TCP"] += 1
        t = parse_tcp(ip["body"])
        if t is None:
            truncated += 1
            continue
        # 非首片的分片里没有完整 TCP 头，跳过但计数
        if ip["frag_off"]:
            ip_proto["TCP(分片)"] += 1
            continue

        a = "%s:%d" % (ip["src"], t["sport"])
        b = "%s:%d" % (ip["dst"], t["dport"])
        conv["%s -> %s" % (a, b)] += 1

        if args.port is not None and args.port not in (t["sport"], t["dport"]):
            continue
        if args.ip is not None and args.ip not in (ip["src"], ip["dst"]):
            continue
        tcp_segs.append({
            "ts": ts, "src": ip["src"], "dst": ip["dst"],
            "sport": t["sport"], "dport": t["dport"], "seq": t["seq"],
            "ack": t["ack"], "flags": t["flags"], "win": t["win"],
            "payload": t["payload"],
        })

    # ------------------------------------------------------------------ 概览
    print("=" * 78)
    print("文件      %s" % args.pcap)
    print("链路类型  %d（%s）  snaplen=%d" % (
        linktype, {1: "Ethernet", 113: "Linux SLL", 0: "loopback", 108: "loopback"}.get(linktype, "未知"),
        snaplen))
    print("帧数      %d    时间跨度 %.3f s" % (len(frames), t_end - t0))
    print("协议      " + "  ".join("%s=%d" % kv for kv in ip_proto.most_common()))
    if truncated:
        print("截断      %d 段（抓包被杀时的尾巴，已忽略）" % truncated)

    flt = []
    if args.port is not None:
        flt.append("port %d" % args.port)
    if args.ip is not None:
        flt.append("ip %s" % args.ip)
    print("过滤器    %s   ->  命中 %d 段 TCP" % ("  ".join(flt) or "(无)", len(tcp_segs)))

    if conv and (args.port is not None or args.ip is not None):
        want = str(args.port) if args.port is not None else None
        shown = [(k, v) for k, v in conv.most_common()
                 if (want is None or (":%s ->" % want in k + " ->") or k.endswith(":%s" % want))
                 or (args.ip is not None and args.ip in k)]
        if shown:
            print("\n会话（该端口/IP 相关）")
            for k, v in shown[:12]:
                print("  %-46s %5d 帧" % (k, v))
    elif conv:
        print("\n会话 Top 10")
        for k, v in conv.most_common(10):
            print("  %-46s %5d 帧" % (k, v))

    if not tcp_segs:
        print("\n没有匹配的 TCP 段。检查：端口写错了？抓包之后跑过 curl 吗？")
        return

    # 流重组：时间线打标签和后面的判定都靠它
    streams = build_streams(tcp_segs)
    records = {k: parse_records(st["buf"], st["have"]) for k, st in streams.items()}

    # ---------------------------------------------------------------- 时间线
    if not args.summary_only:
        print("\n" + "-" * 78)
        print("时间线（seq 相对各自的 ISN；len 是 payload 长度，不含 TCP 头）")
        print("-" * 78)
        shown = 0
        for idx, s in enumerate(tcp_segs):
            if shown >= args.max:
                print("... 还有 %d 段没打印，用 --max 调大" % (len(tcp_segs) - shown))
                break
            key = (s["src"], s["sport"], s["dst"], s["dport"])
            st = streams[key]
            off = st["off"].get(idx)
            rel = (s["seq"] - st["isn"]) & 0xFFFFFFFF
            if rel > 0x7FFFFFFF:
                rel -= 0x100000000  # 回绕成负值，便于看"落后多少"
            plen = len(s["payload"])
            line = "+%8.3fs  %-21s > %-21s %-5s seq=%-10d ack=%-10d len=%-5d win=%d" % (
                s["ts"] - t0,
                "%s:%d" % (s["src"], s["sport"]),
                "%s:%d" % (s["dst"], s["dport"]),
                flags_str(s["flags"]), rel, s["ack"], plen, s["win"])
            tag = tag_for(off, plen, records[key]) if off is not None else ""
            if tag:
                line += "  [" + tag + "]"
            print(line)
            if args.data > 0 and plen:
                print("            " + hexdump(s["payload"], args.data))
            shown += 1

    # ------------------------------------------------------------------ 统计
    print("\n" + "-" * 78)
    print("统计")
    print("-" * 78)

    dirs = OrderedDict()
    for s in tcp_segs:
        key = (s["src"], s["sport"], s["dst"], s["dport"])
        d = dirs.setdefault(key, {
            "segs": 0, "bytes": 0, "fin": 0, "rst": 0, "syn": 0,
            "retx": 0, "gaps": [], "next": None, "seen": set(),
        })
        d["segs"] += 1
        plen = len(s["payload"])
        if plen:
            d["bytes"] += plen
            if s["seq"] in d["seen"]:
                d["retx"] += 1
            d["seen"].add(s["seq"])
            if d["next"] is not None:
                if s["seq"] == d["next"]:
                    pass
                elif s["seq"] > d["next"]:
                    d["gaps"].append((d["next"], s["seq"] - d["next"]))
            d["next"] = s["seq"] + plen
        if s["flags"] & 0x01:
            d["fin"] += 1
        if s["flags"] & 0x04:
            d["rst"] += 1
        if s["flags"] & 0x02:
            d["syn"] += 1

    for key, d in dirs.items():
        print("  %s:%d -> %s:%d" % key)
        print("      段 %-4d payload %-7d FIN %-2d SYN %-2d RST %-2d 重传 %d" % (
            d["segs"], d["bytes"], d["fin"], d["syn"], d["rst"], d["retx"]))
        for start, length in d["gaps"][:6]:
            print("      ! 序号空洞 %d 字节（从相对 %d 开始）—— 这段在路上丢了"
                  % (length, start))

    # ------------------------------------------------------ 逐连接判定
    # 这一节是回答"到底谁没说话、谁先挂"的地方。分两类故障：
    #   A. 我们的请求没发出去 / 发漏了   -> 看 cli_bytes 和重传
    #   B. 服务端等不及先关了连接         -> 看"服务端 FIN"早于"我们发 Finished"
    # B 类在 trace 上的形状非常固定：服务端 flight 收齐 → 长时间空白 → 服务端 FIN
    # → 我们才发 Finished。空白期是我们本地算力（验签），不是网络。
    rows = analyze(tcp_segs)
    if rows:
        print("\n" + "-" * 78)
        print("逐连接（客户端 = 端口大的一侧）")
        print("-" * 78)
        print("  %-6s %-13s %-8s %-10s %-12s %-11s %s" % (
            "端口", "服务端payload", "我们payload", "收齐→Finished", "握手总时长", "服务端FIN", "判定"))
        for r in rows:
            print("  %-6d %-13s %-8s %-12s %-12s %-11s %s" % (
                r["port"],
                "%dB/%d段" % (r["srv_bytes"], r["srv_segs"]),
                "%dB" % r["cli_bytes"],
                ("%.2fs" % r["stall"]) if r["stall"] is not None else "-",
                ("%.2fs" % r["hs_total"]) if r["hs_total"] is not None else "-",
                ("SYN+%.2fs" % r["deadline"]) if r["deadline"] is not None else "(未提前关)",
                r["verdict"]))

        races = [r for r in rows if r["race"]]
        oks = [r for r in rows if r["ok"]]
        print("\n  === 结论线索 ===")
        if races:
            print("  %d/%d 条连接是「服务端等不及先关了」：我们本地停顿 %.2f–%.2fs，"
                  % (len(races), len(rows),
                     min(r["stall"] for r in races), max(r["stall"] for r in races)))
            print("  而服务端从 SYN 起算只给 %.2f–%.2fs（跨连接几乎是个常数，就是它的握手期限）。"
                  % (min(r["deadline"] for r in races), max(r["deadline"] for r in races)))
            print("  -> 不是丢包、不是交付问题：TLS 1.3 客户端必须先在本地验完证书链才发")
            print("     Finished，我们验签慢到超过对端期限，对端按空闲关连接。要提速或换")
            print("     更有效的验签实现，别在 sr_input / recv 里找 bug。")
            if oks:
                print("  -> 成功的 %d 条停顿都在 %.2fs 以内，正好卡在期限下方，是同一场赛跑。"
                      % (len(oks), max(r["stall"] for r in oks if r["stall"] is not None)))
        elif oks:
            print("  %d/%d 条连接双向都有数据且服务端有响应。发送、接收、交付三段都通。"
                  % (len(oks), len(rows)))
        else:
            print("  没有一条是干净的：逐条看上面的 payload 与重传。")
        for r in rows:
            if r["cli_bytes"] == 0:
                print("  ! 端口 %d：我们一个数据字节都没发（只有 SYN/ACK/FIN）" % r["port"])
            if r["retx_cli"]:
                print("  ! 端口 %d：发出方向 %d 次重传" % (r["port"], r["retx_cli"]))
            if r["rst"]:
                print("  ! 端口 %d：出现 RST" % r["port"])


def client_key(s):
    """(连接四元组[客户端在前], 本段是否客户端方向)。客户端 = 端口大的一侧。"""
    if s["sport"] >= s["dport"]:
        return (s["src"], s["sport"], s["dst"], s["dport"]), True
    return (s["dst"], s["dport"], s["src"], s["sport"]), False


def analyze(tcp_segs):
    conns = OrderedDict()
    for idx, s in enumerate(tcp_segs):
        ck, is_cli = client_key(s)
        c = conns.setdefault(ck, {"cli": [], "srv": []})
        c["cli" if is_cli else "srv"].append(idx)

    rows = []
    for ck, c in conns.items():
        cli_bytes = sum(len(tcp_segs[i]["payload"]) for i in c["cli"])
        srv_pay = [i for i in c["srv"] if tcp_segs[i]["payload"]]
        srv_bytes = sum(len(tcp_segs[i]["payload"]) for i in srv_pay)
        retx = 0
        seen = set()
        for i in c["cli"]:
            s = tcp_segs[i]
            if s["payload"]:
                if s["seq"] in seen:
                    retx += 1
                seen.add(s["seq"])

        # 我们发 Finished 的时刻。TLS 1.3 客户端先发 ChangeCipherSpec（记录类型
        # 0x14）再发 Finished；OpenSSL 常把两者塞进同一段（80 字节 = CCS 6 +
        # Finished 74），所以"第一条以 0x14 开头的客户端段"就是它。
        # 不依赖记录解析，这样即使 trace 里的 payload 被截断也能用。
        cli_pay = [i for i in c["cli"] if tcp_segs[i]["payload"]]
        t_fin = None
        for i in cli_pay:
            p = tcp_segs[i]["payload"]
            if p[0] == 0x14 and len(p) >= 6:
                t_fin = tcp_segs[i]["ts"]
                break
        if t_fin is None and len(cli_pay) >= 2:
            # 没有 CCS（不做 middlebox 兼容的实现）：ClientHello 之后那段就是 Finished
            t_fin = tcp_segs[cli_pay[1]]["ts"]

        # 服务端 flight 收齐 = 我们发 Finished 之前最后一个服务端 payload 段
        t_flight = None
        if t_fin is not None:
            before = [i for i in srv_pay if tcp_segs[i]["ts"] < t_fin]
            if before:
                t_flight = tcp_segs[before[-1]]["ts"]
        if t_flight is None and srv_pay:
            t_flight = tcp_segs[srv_pay[-1]]["ts"]

        t_syn = min(tcp_segs[i]["ts"] for i in c["cli"] if tcp_segs[i]["flags"] & 0x02) \
            if any(tcp_segs[i]["flags"] & 0x02 for i in c["cli"]) else None
        srv_fin = [i for i in c["srv"] if tcp_segs[i]["flags"] & 0x01]
        t_srv_fin = tcp_segs[srv_fin[0]]["ts"] if srv_fin else None
        resp = [i for i in srv_pay if t_fin is not None and tcp_segs[i]["ts"] > t_fin]
        rst = any(tcp_segs[i]["flags"] & 0x04 for i in c["cli"] + c["srv"])

        stall = (t_fin - t_flight) if (t_fin is not None and t_flight is not None) else None
        race = t_srv_fin is not None and t_fin is not None and t_srv_fin < t_fin
        # 只有"早于我们发 Finished"的 FIN 才是信号；正常收尾（我们发完 FIN 对方回 FIN）
        # 的不占这一列，否则会把成功的连接显示成"flight+8.17s"，看着像故障。
        deadline = (t_srv_fin - t_syn) if (race and t_syn is not None) else None

        if cli_bytes == 0:
            verdict = "只握手，没发数据"
        elif race:
            verdict = "失败·服务端先 FIN（我们迟到 %.2fs）" % (t_fin - t_srv_fin)
        elif t_fin is None:
            verdict = "失败·我们从未发 Finished"
        elif resp:
            verdict = "成功"
        else:
            verdict = "可疑·Finished 已发但服务端没回数据"

        rows.append({
            "port": ck[1], "cli_bytes": cli_bytes, "srv_bytes": srv_bytes,
            "srv_segs": len(srv_pay), "stall": stall, "deadline": deadline,
            "race": race, "ok": verdict == "成功", "verdict": verdict,
            "hs_total": (t_fin - t_syn) if (t_fin is not None and t_syn is not None) else None,
            "retx_cli": retx, "rst": rst,
        })
    return rows


if __name__ == "__main__":
    main()
