[← 返回 RmikuOS 主页](../README.md)

---

## Network Stack

RmikuOS 自带一套 TCP/IP 协议栈：自 virtio-net 驱动起，经 Ethernet / ARP / IPv4 / ICMP / UDP / TCP 与 DHCP，到一组专用的 socket 系统调用（号段 100–109），最终在用户态跑起一个真实的 HTTP 服务器与 TFTP 客户端——宿主机浏览器经 QEMU `hostfwd` 直接访问 guest 内的页面，两台 QEMU 经 socket pair 互 ping。协议栈每一层都是内核 `drivers/net/` 下的 Rust 代码，不依赖任何外部网络 crate。

```text
用户态   httpd(静态文件 + JSON API)   tftp(文件注入)   ping / udp_test / tcp_test
            │  socket syscalls:100 SOCKET … 109 RECV(专用网络号段)
────────────┼───────────────────────────────────────────────────
内核       Socket 层(UDP / TCP 统一 socket table,端口冲突检查)
            │
            ├─ TCP   11 态状态机 · 滑动窗口 · Jacobson/Karn 自适应 RTO
            ├─ UDP   无连接收发 · 校验和
            └─ ICMP  echo request / reply(ping)
            │
            IPv4   头部校验和 · 按 protocol 字段分发(17=UDP,6=TCP,1=ICMP) · 同网段直连路由
            DHCP   四步交互(DORA)· 广播位 · options 解析,自动配置地址
            ARP    地址缓存 + 挂起队列(未命中先存整包,解析成功补发)
            │
            Ethernet → virtio-net 驱动 → QEMU slirp → 宿主机协议栈
```

QEMU 侧使用 slirp 用户态网络（`-netdev user`）：无需宿主机 root 权限，自带 DHCP 服务器（`10.0.2.2`）与 DNS（`10.0.2.3`），guest 默认落在 `10.0.2.15`。

### ARP：挂起队列（Pending Queue）

发包时 ARP 缓存未命中是常态，而地址解析是异步的。朴素实现直接丢包、把重试责任推给上层；RmikuOS 在 ARP 层内置一张 4 槽 PENDING 队列：未命中时整包入队并发出 ARP request，`on_arp_learned` 回调时补发，上层（IP / TCP / UDP）完全无感。实现上有一条锁纪律：回调点不得持有 ARP 缓存锁，否则会触发自研锁的同核重入死锁检测。

### IPv4 与校验和

* 发送时生成头部校验和、接收时验证；checksum 写回必须显式转大端——slirp 对校验失败的包**静默丢弃**（实踩的坑）；
* 本机地址原子化（`MY_IP`）：DHCP 完成前用默认值，租约落地后 `set_my_ip` 热切换；
* 按 protocol 字段分发到 UDP（17）/ TCP（6），未知协议打日志——漏写分发行曾导致 SYN-ACK 静默消失，这类坑必须能被一眼看见。

### TCP：教学版实现


* 11 态状态机（Closed → Listen → SynSent / SynReceived → Established → FinWait1 / 2 → CloseWait / Closing / LastAck → TimeWait），主动 open（connect）与被动 open（listen / accept）均支持；
* 发送侧：`tx_unacked` 重传队列（SYN / FIN 各占一个序号），**Jacobson/Karn 自适应 RTO**（RFC 6298 定点 SRTT/RTTVAR，RTO = SRTT + max(G, 4·RTTVAR)，200ms–16s，指数退避，最多 8 次；详见 Network Experiments 一节）；
* 接收侧：按序交付 + 固定窗口广告（65535），乱序段丢弃并重复 ACK；
* 定时器不依赖硬件中断：RTO / TIME_WAIT 等全部期限由 socket 层 `poll()` 内嵌的 `tick()` 驱动；
* 两条路径均已实机验证：主动 connect 经 slirp 访问宿主机 `nc -l`；被动 listen 经 hostfwd 接受宿主机浏览器连接。


### DHCP 客户端

内核态 DHCP 客户端：BOOTP 236 字节头 + magic cookie（`99,130,83,99`）+ options 编解码（53 消息类型 / 50 请求地址 / 54 服务器标识 / 55 参数请求 / 3 网关 / 6 DNS / 51 租期）。flags 置广播位 `0x8000`，使 OFFER / ACK 走二层广播——租约落地之前本机没有合法地址，单播回复无从送达。四步交互后 `set_my_ip(yiaddr)`，实测租得 `10.0.2.15`、租期 86400s。

### ICMP 与双机互 ping

ICMP echo request / reply 入栈后，两台 QEMU 可以直接对话：经 socket netdev pair（`-netdev socket,listen=` / `connect=`）二层直连、绕开 slirp，两台 guest 互设 `192.168.100.x` 后 ping 通。这一步抓出两个隐蔽 bug：

* **MAC 硬编码**：`eth.rs` 的 `MY_MAC` 写死了 slirp 模式分配的 `52:54:00:12:34:56`，而 socket pair 模式分配的是 `...0A` / `...0B`——A 机的包发出去了，B 机网卡不认这个源；
* **同网段路由**：`ip.rs` 的 `next_hop` 把同 /24 的地址也交给网关，ARP who-has 的始终是 `10.0.2.2` 而非对端——同网段应当直连，下一跳即目的地址本身。

两个 bug 都由「三段定位法」在 ARP 层现形：tcpdump 里 ARP 请求的目标地址暴露了一切。

### Socket 系统调用：100–109 专用号段

网络调用不挤占主系统调用表，独立开一段号段——主分发只多一条范围判断，后续扩充也不污染既有编号：

```text
100 SOCKET    101 BIND    102 SENDTO    103 RECVFROM    104 CLOSE
105 CONNECT   106 LISTEN  107 ACCEPT    108 SEND        109 RECV
```

用户态经 `net.h` 封装为 `net_socket()` / `net_socket_tcp()` / `net_bind()` / `net_connect()` / `net_listen()` / `net_accept()` / `net_send()` / `net_recv()` 等，体感与 POSIX 对齐。

### httpd：跑在自研协议栈上的 Web 服务器

协议栈的「真应用」验证：一个多文件 C 工程（`httpd.c` / `http.c` / `pages.c` + 头文件），顺带压测了用户态多文件编译与链接——并因此逼出并修复了头文件函数体未加 `static inline` 导致的 `multiple definition` 隐患（单文件时代不可见，多文件链接即炸）。

* **静态文件模式**：`httpd wow.html` 启动时将文件读入内存（16KB 缓冲），`/` 与 `/index.html` 发送文件内容；
* **内联路由**：`/demo` 内联演示页、`/hello`、`/api/stats`（JSON 实时请求计数）、404；
* **HTTP 细节自己扛**：TCP 是字节流，请求头边界靠扫描 `\r\n\r\n` 确定；发送超 1460 字节按 1400 切片；`Connection: close` 迭代式服务；
* **浏览器适配**：Chrome 会打开「占位不说话」的预热连接，迭代式服务器 accept 到它就会被焊死——recv 增加软超时（800ms），空连接到点踢掉，真实请求随后即被服务。

宿主机访问只需在 run.sh 的 netdev 上挂一行 port forward（**必须与 `id=net0` 同行**，拆成独立参数会被 QEMU 误认为磁盘镜像）：

```text
-netdev user,id=net0,hostfwd=tcp::8080-:8080
```

```text
/ $ httpd wow.html
[httpd] loaded wow.html, 6986 bytes, serving at /
[httpd] RmikuOS httpd listening on 10.0.2.15:8080
[httpd] #1 GET /
[httpd] #1 served, 6986 bytes, closing fd=2
```

浏览器打开 `http://127.0.0.1:8080/` 即可（内联演示页在 `/demo`）。随附的 `wow.html` 演示页每 2 秒 `fetch('/api/stats')` 刷新请求计数——页面上数字每跳一次，背后都是一次完整的 TCP 建立—传输—挥手。

### TFTP：经 slirp 的文件注入通道

rootfs 只读、重新打包 ext4 镜像太慢，实验文件（尺寸扫描用的 4K–1M 随机文件）需要一条运行时注入通道。slirp 内置 TFTP 服务器：在 netdev 上挂 `tftpboot=<绝对路径>`（必须是绝对路径，相对路径直接报 `Invalid parameter`），guest 内用户态 `tftp` 客户端（RRQ → DATA/ACK 停等）即可拉取宿主机目录里的文件：

```text
/ $ tftp hello.txt /tmp/a
tftp: hello.txt -> /tmp/a, 26 bytes
```

一个与文档印象不符的实测结论：**slirp 的 TFTP 服务器在 guest 视角是 `10.0.2.2`**（与 DHCP 网关同地址），而不是某些资料里的 `10.0.2.4`——向后者发 ARP who-has 永远无人应答，改指 `10.0.2.2` 即通。ACK 直接回 `recvfrom` 的 from 地址，TFTP 的 TID 语义天然正确。

### NTP 客户端：网络同步时间（墙钟 + 文件时间戳）

RmikuOS 通过网络协议拿到真实世界时间：宿主机的 Python NTP 服务器 + guest 内的用户态 `ntpdate` 客户端，一次校准内核墙钟，之后 `time()` 与文件 `mtime` 都是单调累加的真实 epoch 秒。

```text
宿主机                                    QEMU guest
┌──────────────────┐      slirp      ┌────────────────────────────┐
│ tools/ntp_server │←─ UDP 10.0.2.2 ─│ ntpdate(5次采样最小delay)   │
│  (RFC5905子集)   │── 123/任意端口 ─→│    │ SYS_SET_WALL_CLOCK     │
└──────────────────┘                  │    ▼                       │
                                      │ 内核墙钟(epoch微秒+单调累加) │
                                      │    ├─ time()/gettimeofday  │
                                      │    └─ Stat.mtime → st_mtime│
                                      └────────────────────────────┘
```

#### 原理：RFC 5905 教学子集

* **四时间戳**：`offset = ((T2−T1) + (T3−T4)) / 2`——请求去程与响应回程各带一次"服务器时间减客户端时间"，平均后抵消网络延迟，得到真实时钟偏移（`delay = (T4−T1) − (T3−T2)` 是往返延迟）。
* **64 位定点**：NTP 时间戳高 32 位=秒（1900 纪元）、低 32 位=分数秒（2⁻³²）。分数秒让 delay 可测到毫秒级，否则 delay 全整数秒、"取最小"失去意义。
* **防溢出**：两个 ≈22 亿秒级时间戳相减后相加会超 2⁶⁴——必须 `((T2−T1)>>1) + ((T3−T4)>>1)`（先移位再相加，丢 0.1ns）。
* **最小延迟原则**：5 次采样取 delay 最小那次——网络最空闲 ≈ 去回程最对称 ≈ offset 最准。
* **本地假时钟**：T1/T4 用 `get_time_us()`（单调微秒，自启动起，0 基准）。offset 是"服务器绝对时间 − 本地开机基准"的常数差，校准后 `墙钟 = 单调 + offset`——完美绕开"没时钟"的鸡生蛋问题。

#### 墙钟与 Stat 时间戳

内核 `timer` 维护墙钟：`set_wall_clock(epoch_us)` 存"校准时刻的绝对微秒 + 单调微秒快照"，`now_secs()` 单调累加（未校准返回 0）。`Stat` 新增 `mtime` 字段（复用原 reserved[4]，32 字节布局不变），各文件系统 `stat()` 填 `now_secs()`；用户态 `fs.h` 翻译层填 `st_mtime`（atime/ctime 教学简化同 mtime）。`time()`/`gettimeofday` 也改接墙钟（新 syscall `SYS_GET_EPOCH`）。

#### 使用

```bash
# 宿主机（tools/ntp_server.py, RFC 5905 教学子集服务器）：
sudo python3 tools/ntp_server.py          # 端口 123(需 root); 或 -p 12300 免 root
```

```text
/ $ ntpdate                              # 默认 10.0.2.2:123; 或 ntpdate 12300
[ntpdate] synced: epoch=1785658218 s, delay=2 ms
/ $ sqlite3 /fat/test.db                 # 落盘库（CREATE TABLE 需 VFS xOpen 判 pOutFlags）
sqlite> CREATE TABLE t(x);  INSERT INTO t VALUES(42);  .quit
/ $ /samples/stat_time /fat/test.db
path          : /fat/test.db
mtime(epoch)  : 1785658405
time()(epoch) : 1785658405
mtime(GMT)    : 2026-08-02 08:13:25
```

QEMU slirp 关键路径：guest 发 UDP 到 `10.0.2.2:<端口>` 会被自动转发到宿主机 loopback 同名端口（无需 hostfwd），与 TFTP/DHCP 同一通道。

#### 已知局限（教学取舍）

* **QEMU TCG 虚拟时钟**：TCG 动态翻译下 `time` 寄存器按虚拟时间推进，负载不同可能与真实时钟速率不一致（delay 异常大时 epoch 有百秒级偏差）。真实硬件 timebase 与晶振绑定，无此问题。
* **闰秒 / 2036 纪元回绕**：RFC 5905 用 era 判断（服务器时间戳在本地 ±68 年内则判定回绕 +2³²）处理；教学版注释说明不实现。
* **FAT 目录项 DOS 时间戳未读**：mtime 取 stat 时刻墙钟而非磁盘持久化修改时间（需 DOS→epoch 转换，留待以后）。
* **相关 syscall 号段**：`SYS_SET_ECHO=69`、`SYS_SIGNAL=70`、`SYS_SET_FRONT=71`、`SYS_GET_TIME_US=72`、`SYS_SET_WALL_CLOCK=73`、`SYS_GET_EPOCH=74`。

### wget：TCP 客户端从 host 拉文件（网络栈的下载闭环）

`user/c/wget/wget.c`（约 150 行）——用自研 TCP 栈发 `HTTP/1.0 GET`，把响应体存进 FAT：

```
wget http://10.0.2.2:8000/hello.txt /fat/hi.txt   # URL 形式(默认端口 80)
wget http://10.0.2.2:8000                          # 无路径 -> GET /
wget 10.0.2.2 8000 /hello.txt /fat/hi.txt          # 旧三参形式(兼容)
```

流程：`socket → connect → send GET（HTTP/1.0 + Connection: close）→ 收响应头（缓冲找 \r\n\r\n）→ 收 body 到连接关闭 → write 落盘`。host 侧 `python3 -m http.server 8000` 即服务端——guest 访问 `10.0.2.2:8000` 经 slirp 转发到 host loopback（与 NTP 同款通道，无需 hostfwd）。

实测输出：

```
[tcp] fd 1 established
wget: HTTP/1.0 200 OK
Server: SimpleHTTP/0.6 Python/3.14.4
Content-Length: 17
...
/ $ cat /fat/hi.txt
hello from host!
```

#### 顺带把内核 recv 改成 POSIX 语义

wget 调试暴露了内核一个 API 简化：`tcp::recv_data` 原来**弹出整个 TCP chunk、只返回请求长度**——逐字节读的客户端（如 HTTP 头解析）会丢数据。已修复为 POSIX 语义：消费 n 字节后 `chunk.drain(..n) + push_front` 把剩余放回队列头，下次 recv 继续读。UDP 数据报语义本就正确、未动。**现在任意读法（大块/逐字节）都安全**。

#### 踩坑两个

* **双重 htons**：`addr_of` 内部已 `htons(port)`，再套一层会转回主机序——8000(0x1F40) 变 16415，SYN 发错端口。tcp_test 直传端口所以从没暴露；
* **整块弹出丢数据**：见上——客户端必须大块读（现已在 API 侧根治）。

---

### 排障方法学：三段定位法

网络问题一律按「guest 发没发对 → slirp 转没转发 → 宿主机谁收走」三段切分：

```text
-object filter-dump,id=f0,netdev=net0,file=/tmp/rmiku.pcap   # guest 网线上抓包
sudo tcpdump -i lo -nn -X udp port 9999                      # slirp 是否已转发到宿主机
ss -ulnp | grep 9999                                         # 宿主机端口被谁持有
```

实战战绩：曾用这套方法揪出「4 个僵尸 nc 进程同绑一个 UDP 端口、报文全进旧进程接收队列」——guest 侧报文逐字节验证完美，锅在宿主机。

---

## Network Experiments：TCP RTO 对照实验

网络栈的第二组实验回答一个问题：**重传超时（RTO）的估计方式，对真实传输性能影响多大？** 对照双方共享除估值器以外的全部代码（同一状态机、同一窗口管理、同一丢包装置），唯一变量是 RTO 算法：

```text
new:  Jacobson/Karn 自适应 RTO(RFC 6298 风格,定点实现)
      SRTT/RTTVAR 按 ×8/×4 缩放存储,除法即右移,无浮点
      RTO = SRTT + max(G, 4·RTTVAR),clamp [200ms, 16s],G = 10ms(tick 粒度)
      Karn 两条:重传段不采样(ACK 歧义);退避期间保持翻倍后的 RTO
      队首采样规则:每个 ACK 只在弹出队首段时采样(队首干净 ⟹ 样本新鲜)
      RTO-restore:有前向进展但无干净样本时恢复估值(参考 Linux)

old:  固定 RTO = 1s,指数退避,封顶 16s(实现 Jacobson 之前的原版)
```



### 实现过程中抓出的三个深邃 bug

* **陈旧样本死亡螺旋**：串行修洞（每 tick 只重传队首）下，洞修好后的累积 ACK 会连跳弹出多个老段；若对每个被确认的段都采样，`now − sent_ms` 里混入了等待修洞的时间，假样本按等比数列膨胀（实测 100→202→422→…→254659ms），RTO 一路爆炸到封顶。修复即「队首采样规则」：队首段干净意味着它发出未满一个 RTO，样本必然新鲜。
* **退避棘轮**：Karn 的「退避保持到下一个干净样本」与队首采样叠加后，排水期（数据已发完、只剩重传在飞）永远采不到干净样本，RTO 每个洞翻倍一次，几洞之内钉死在 16s。修复即「RTO-restore」：只要有前向进展就恢复估值——旧版固定 RTO 天然等价于此，这也是对照实验公平的一环。
* **无流量控制**：初版 `send_data` 只看对端窗口是否为零、不跟踪在途字节，1MB 传输瞬间灌爆宿主 64KB 接收窗，真实丢包与注入丢包混杂，实验不可解释。修复为阻塞式窗口管理（`in_flight = snd_nxt − snd_una`，窗口满则解锁 poll 等待）。

### 实验装置

* **确定性丢包**：`LOSS_EVERY = N` 时每 N 个数据段丢弃 1 个（SYN / FIN 不丢），完全可复现；
* **单变量对照**：对照版与新版共享丢包装置、流量控制与打点，只差估值器；
* **自造噪声消除**：正式测量前 30 个 4K 请求预热，每次 run 间隔 2s——否则 8 槽 socket 表被 TIME_WAIT（10s）子连接占满，SYN 被静默丢弃，宿主退避产生周期性 18s 停摆；
* 每组 5–7 次取中位数；脚本：`tcp_exp.sh`（丢包率组）/ `tcp_size_sweep.sh`（尺寸组）/ `plot_tcp.py`（绘图），数据落盘 `logs/tcp/`。

### 实验一：尺寸扫描 @ 0% 注入丢包（4K – 1M）

![size sweep](../logs/tcp/fig1_size_sweep.png)

* ≤64K：两版差异在 ±10% 噪声带内——**do no harm**，自适应估值器在无损路径上不引入额外开销；
* ≥128K：new 在两个独立 session 中稳定更快（1M：22.4s / 21.7s vs 28.2s / 48.8s），方向可复现，幅度受宿主噪声影响，给区间不给单点。

![cross-session drift](../logs/tcp/fig2_drift.png)

跨 session 漂移（同版本连跑两批）：new 的 1M 中位数漂移 **−3%**，old 的 1M 漂移 **+73%**；小尺寸双方均在 ±30% 噪声带内。注意两批的运行顺序与版本相关（位置效应未消），此图作为 observation 呈现，交错重复实验见 Roadmap。

### 实验二：丢包率扫描 @ 100K（0 / 5 / 10 / 20%）

![loss sweep](../logs/tcp/fig3_loss_sweep.png)

| 注入丢包率 | new（中位数） | old（中位数） | 提速      |
| ---------- | ------------- | ------------- | --------- |
| 0%（对照） | 2.052s        | 2.111s        | 1.03×     |
| 5%         | 2.253s        | 5.425s        | **2.41×** |
| 10%        | 3.187s        | 10.765s       | **3.38×** |
| 20%        | 5.770s        | 23.243s       | **4.03×** |

* 0% 对照臂两版一致（1.03×），实验台自证干净；
* old 耗时随丢包率近似线性爆炸（≈ 每 1% 丢包 +1.05s），正是固定 1s RTO「每洞罚一秒」的理论预期；new 的 RTO 收敛在 200ms 附近，曲线平缓；
* 机制佐证：逐洞恢复耗时 new ≈ 200ms/洞、old ≈ 1s+/洞（恢复比 5–9×）；且 new 的恢复时间随洞序号线性爬升——这是串行修洞的排队签名，也是 Roadmap 中快重[docs] readme更新cubic实验传 / SACK 的直接动机；
* 采样规模：new 传 1M 采 5236 个 RTT 样本，old 全程 0 个（它没有估值器）——对照的本质浓缩在这一数字里。

## TCP CUBIC 拥塞控制实验

在 RmikuOS 教学 TCP 栈上实现 **CUBIC(RFC 9438)** 拥塞控制与快速重传,并与无拥塞控制版本在确定性丢包下做 A/B 对照。

### 实验设计

- **对照组**:old 版(无 cwnd,仅接收窗口流控 + RTO 重传)
- **实验组**:CUBIC 版(慢启动 + 立方增长 + β=0.7 降窗 + 快速收敛 + 3-dupACK 快速重传)
- **丢包装置**:发送侧每 `LOSS_EVERY` 个数据段丢 1 个(确定性、可复现),档位 {0, 20, 50, 100, 200, 500}
- **负载**:guest 内 httpd 发文件,宿主机 curl 下载,尺寸 {64K, 256K, 1M},每格 7 次取中位数
- **打点**:每连接一行 `[tcp-stat]`(字节/重传/降窗计数),`[cwnd]` 逐次变窗轨迹;宿主机按日志书签切片聚合

### 结果

![CUBIC 锯齿](../logs/tcp/figs/fig1_cwnd_sawtooth.png)

![恢复路径对比](../logs/tcp/figs/fig2_recovery.png)

- 相同丢包序列下两版**重传总量一致**(fig2 柱高),证明装置公平;差异全在恢复路径:**99.6% 的重传由快速重传完成**(l20@1M:fast=277, RTO=1),单次丢包恢复代价从 ≥200ms(RTO_MIN)降至约 0。
- 实测降窗次数与理论丢包数(段数/LOSS)在全部档位吻合(见 fig3),丢包装置与打点计数自洽。
- RTT 样本数随丢包率下降(fig5),符合 Karn 规则(重传段不采样)。

### 复现

```bash
# 终端1: QEMU 输出落盘(每次重启先删旧日志)
rm -f logs/console.log && ./run.sh riscv64 debug 2>&1 | tee logs/console.log
# 终端2: 扫描(LOSS 标签需与内核编译的 LOSS_EVERY 一致)
./scripts/tcp_loss_sweep.sh old   100 7
./scripts/tcp_loss_sweep.sh cubic 100 7
# 出图
python3 scripts/plot_tcp.py
```

### 局限性

- QEMU 内 RTT≈0,协议栈受 CPU/串口限制(~27KB/s),在途数据不足 1 段,cwnd 不构成瓶颈——**计时列仅作参考**,结论以机制计数为准;窗口瓶颈实验需关闭日志并加链路延迟(设计见实验记录)。
- 耗时存在会话级漂移(每次换内核冷启动),跨行绝对值不可比。
- 接收端原为 GBN 行为(乱序丢弃),已由后续的 SR 升级(重组缓存)解决;SACK 选项未实现。


## TCP 接收端升级实验:Go-Back-N vs Selective Repeat

在 RmikuOS 教学 TCP 栈(CUBIC 拥塞控制 + 快速重传)上,将接收端从 **GBN 行为**(乱序即丢弃)升级为 **SR 行为**(乱序进重组缓存,洞补上后顺序交付),在确定性丢包下做 A/B 对照。

### 前置修复

实验前发现通告窗口 65535 与宿主机 slirp 接收缓冲(≈64KB)几乎相等,满窗口发送会打爆宿主缓冲造成**不可控真实丢包**(无损基线 34~54s 剧烈散布)。将通告窗口降至 **16384(11 段)** 后,基线收敛至 29.2s ± 1.2s。此后所有数据均为 16KB 窗口配置。

### 实验设计

- 对照:同一 CUBIC+快速重传内核,仅接收路径不同(丢弃 vs 缓存)
- 丢包:{0, 1/5, 1/6, 1/7, 1/10, 1/20, 1/50, 1/100, 1/200, 1/500} 八档 × 20 次/格,1M 文件
- 每组跑于独立冷启动会话;污染会话(宿主机并行实验导致单调爬升/钟形隆起)整组作废重跑
- 统计:中位数 + IQR,离群点不剔除但由中位数免疫

### 结果

![耗时-丢包率](../logs/sr/figs/fig_sr2_time_vs_loss.png)

![分组箱线图](../logs/sr/figs/fig_sr2_box.png)

| 丢包率       | GBN 中位(s) | SR 中位(s) | 结论                            |
| ------------ | ----------- | ---------- | ------------------------------- |
| 0            | 29.9        | 29.2       | 打平                            |
| 1/5          | 71.3        | 69.8       | 共同触底                        |
| 1/6          | ~35.1       | ~35.9      | 打平，中间态                    |
| **1/7**      | **~35.7**   | **~28.9**  | **SR 快 19%,SR 回到基线水平！** |
| 1/10         | 30.7        | 28.6       | **SR 快 7%**                    |
| 1/20         | 30.4        | 28.6       | **SR 快 6%**                    |
| 1/50 ~ 1/500 | 28.4~28.7   | 28.6~30.5  | 打平                            |




### 分析

1. **SR 的收益集中在 1/10~1/20 重-中丢包档**:此时丢包频繁到 GBN 级联(洞后段被丢弃、逐段等 dup ACK 链式修复)构成可测开销,而又未让 cwnd 触底。
2. **1/5 档两组共同劣化至 ~70s**:每 5 段丢 1 个使 cwnd 长期钉死在 2 段下限,吞吐 ∝ 窗口。定量验证:2/11 ≈ 0.41 ≈ 28.6/69.8——窗口下限主导一切,接收端策略无关。
3. **在 5 和 10 之间挖出了一个悬崖 **：对 SR 来说，1/6 还在 36s 的中间态，1/7 就突然跌回 28.9s 的无损基线水平，一档之差 24%。1/6→1/7 恰好跨过临界点，就是离散动力学里的 regime switch。
4. **轻丢包档打平**:丢包间隔超过窗口,GBN 级联深度 ≈ 0,丢弃与缓存无差异。
5. **SR 收益整体温和(≈7%)的原因**:本环境 RTT≈0、在途段数 ≪ 窗口,且快速重传已将级联修复的等待代价压至近零——SR 相对"GBN+快速重传"的边际收益天然有限。SR 的决定性优势需要足够大的带宽时延积(丢包瞬间洞后存在大量在途段)才能显现,列入后续工作(链路延迟队列,设计已定)。

### 实验教训(数据质量控制)

- 首轮 gbn 数据被宿主机并行任务污染(组内单调爬升 32→55s),整组重跑;教训:**对照组应尽量交错/同窗口运行,或用定标 curl 监控会话漂移**。
- sr@200/500 格仍有少量离群点(46~56s),中位数免疫,均值已标注仅供参考。

### 复现

```bash
./scripts/sr_run.sh gbn 10 20 1M     # 采集(标签须与内核 LOSS_EVERY 一致)
./scripts/sr_run.sh sr  10 20 1M
python3 scripts/plot_sr2.py          # 出图 + 汇总表
```



---

