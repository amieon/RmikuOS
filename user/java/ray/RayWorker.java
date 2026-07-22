// ============================================================
// RayWorker.java —— 光线追踪渲染 worker（每个 band 一个线程）
//
// 纯 int 定点（16.16）数学，零浮点指令，零标准库：
//   - 不用 String 拼接 / StringBuilder / String 实例方法
//   - 不用 <clinit>（所有静态数据懒初始化或编译期常量）
//   - 不用 long / float / double
//
// 线程入口约定：public static void run(int arg)
// arg = band 编号（0 上半幅，1 下半幅）
// 渲染结果写入 /tmp/ray_bandN.txt，由主线程拼接输出。
// ============================================================
public class RayWorker {

    // ---------- 定点常量（fix16 = 真实值 * 65536） ----------
    static final int ONE   = 65536;
    static final int EPS   = 2000;      // 0.0305，自相交回避
    static final int AMB   = 9830;      // 0.15  环境光
    static final int DIFFK = 55705;     // 0.85  漫反射系数
    static final int SPECK = 36044;     // 0.55  高光系数
    static final int LOCALK= 49152;     // 0.75  本体着色权重
    static final int REFLK = 14417;     // 0.22  反射权重

    // ---------- 画面 ----------
    static final int W  = 100;          // 宽（字符列）
    static final int H  = 40;           // 高（字符行）
    static final int RH = 20;           // 每个 band 的行数 = H/2

    // ---------- 相机 ----------
    static final int OX = 0;
    static final int OY = 65536;        // 1.00
    static final int OZ = -183500;      // -2.80
    static final int PITCH = 5898;      // 0.09  俯仰（视线下压，地平线上移）
    static final int TANX  = 58982;     // 0.90  水平半视角 tan
    static final int TANY  = 47185;     // 0.72  垂直半视角 tan（含字符高宽比 2:1 修正）
    static final int FOCAL = 98304;     // 1.50  焦距

    // ---------- 主光源（着色/阴影/高光用）：相机左前方 ----------
    static final int LX = -163840;      // -2.5
    static final int LY = 229376;       //  3.5
    static final int LZ = -32768;       // -0.5

    // ---------- 视觉太阳（仅天空渲染用）：画面右上方 ----------
    static final int SUNX0 = 144179;    // 2.2
    static final int SUNY0 = 98304;     // 1.5
    static final int SUNZ0 = 327680;    // 5.0

    // ---------- 三个球（y 坐标 = 半径，正好落在地面上） ----------
    static final int S1CX = -81920, S1CY = 49152, S1CZ = 124518, S1R = 49152;  // (-1.25, .75, 1.9) r=.75
    static final int S2CX =  75366, S2CY = 36044, S2CZ =  91750, S2R = 36044;  // ( 1.15, .55, 1.4) r=.55
    static final int S3CX =   3277, S3CY = 24904, S3CZ =  49152, S3R = 24904;  // ( 0.05, .38, 0.75) r=.38

    // 反照率（决定 ASCII 明暗密度的"材质"）
    static final int A1 = 55705;        // 0.85
    static final int A2 = 45875;        // 0.70
    static final int A3 = 36044;        // 0.55
    static final int CHKA = 52428;      // 棋盘亮格 0.80
    static final int CHKB = 19660;      // 棋盘暗格 0.30

    // 太阳圆盘阈值（dot(视线, 太阳方向)）
    static final int SUNHOT  = 65400;   // 0.998
    static final int SUNWARM = 65250;   // 0.9955

    // ---------- 懒初始化的静态数据（避开 <clinit>） ----------
    static byte[] grad = null;          // ASCII 灰度表 " .:-=+*#%@"
    static int sunX, sunY, sunZ;        // 归一化的太阳方向
    static int rays;                    // 本线程（= 本 JVM）光线计数

    // normalize 的公共暂存（每个线程是独立 JVM，单线程，安全）
    static int[] NV = null;

    static void lazyInit() {
        if (grad != null) return;
        grad = new byte[10];
        grad[0] = ' ';
        grad[1] = '.';
        grad[2] = ':';
        grad[3] = '-';
        grad[4] = '=';
        grad[5] = '+';
        grad[6] = '*';
        grad[7] = '#';
        grad[8] = '%';
        grad[9] = '@';
        NV = new int[3];
        // 太阳方向 = normalize(视觉太阳位置)
        normalize(SUNX0, SUNY0, SUNZ0);
        sunX = NV[0]; sunY = NV[1]; sunZ = NV[2];
    }

    // ---------- 定点算术 ----------
    // 乘法：16.16 * 16.16，拆分高低 16 位防溢出
    // 低位项用 >>4 保留更多精度（4095*4095 不会溢出）
    static int fmul(int a, int b) {
        int ah = a >> 16, bh = b >> 16;
        int al = a & 0xFFFF, bl = b & 0xFFFF;
        return ((ah * bh) << 16) + ah * bl + bh * al + (((al >> 4) * (bl >> 4)) >> 8);
    }

    // 除法：先抬 8 位再除再抬 8 位（|a| < 2^23 时安全）
    static int fdiv(int a, int b) {
        return ((a << 8) / b) << 8;
    }

    // 整数平方根（牛顿迭代）
    static int isqrt(int n) {
        if (n <= 0) return 0;
        int r = n;
        for (int i = 0; i < 40; i++) {
            int q = n / r;
            if (q >= r) break;
            r = (r + q) >> 1;
        }
        return r;
    }

    // fix16 平方根：先左移 8 位再开根，精度 16 倍于直接开根
    // （要求 x < 2^23，本程序所有输入都满足）
    static int fsqrt(int x) {
        return isqrt(x << 8) << 4;
    }

    static int dot(int ax, int ay, int az, int bx, int by, int bz) {
        return fmul(ax, bx) + fmul(ay, by) + fmul(az, bz);
    }

    // 归一化三维向量，结果放 NV[0..2]
    // 零/退化向量（定点误差导致）给一个安全默认值，绝不除零
    static void normalize(int x, int y, int z) {
        int len = fsqrt(dot(x, y, z, x, y, z));
        if (len < 256) {
            NV[0] = 0; NV[1] = ONE; NV[2] = 0;
            return;
        }
        int inv = fdiv(ONE, len);
        // 牛顿迭代一步提升倒数精度：inv *= (2 - len*inv)
        inv = fmul(inv, 2 * ONE - fmul(len, inv));
        NV[0] = fmul(x, inv);
        NV[1] = fmul(y, inv);
        NV[2] = fmul(z, inv);
    }

    // ---------- 求交 ----------
    // 光线 vs 球：oc = 原点-球心，r2 = 半径^2，d 已归一化
    // 返回最近正 t，未命中返回 -1
    static int sphT(int ocx, int ocy, int ocz, int r2,
                    int dx, int dy, int dz) {
        int b = dot(ocx, ocy, ocz, dx, dy, dz);
        int c = dot(ocx, ocy, ocz, ocx, ocy, ocz) - r2;
        int disc = fmul(b, b) - c;
        if (disc <= 0) return -1;
        int sd = fsqrt(disc);
        int t = -b - sd;
        if (t < EPS) t = -b + sd;
        if (t < EPS) return -1;
        return t;
    }

    // 场景最近命中结果（静态暂存，单线程安全）
    static int HT, HO;   // HO: 1/2/3 = 球，4 = 地面，-1 = 未命中

    static void sceneHit(int ox, int oy, int oz, int dx, int dy, int dz) {
        int best = 0x7FFFFFFF;
        int obj = -1;
        int t;

        t = sphT(ox - S1CX, oy - S1CY, oz - S1CZ, fmul(S1R, S1R), dx, dy, dz);
        if (t > 0 && t < best) { best = t; obj = 1; }
        t = sphT(ox - S2CX, oy - S2CY, oz - S2CZ, fmul(S2R, S2R), dx, dy, dz);
        if (t > 0 && t < best) { best = t; obj = 2; }
        t = sphT(ox - S3CX, oy - S3CY, oz - S3CZ, fmul(S3R, S3R), dx, dy, dz);
        if (t > 0 && t < best) { best = t; obj = 3; }

        if (dy < 0) {                     // 地面 y=0（视线向下才相交）
            t = fdiv(-oy, dy);
            if (t > EPS && t < best) { best = t; obj = 4; }
        }
        HT = best;
        HO = obj;
    }

    // 阴影测试：从 p 沿 ldir 看是否被任一球挡住（ld = 到光源的距离）
    static boolean inShadow(int px, int py, int pz,
                            int ldx, int ldy, int ldz, int ld) {
        int t;
        t = sphT(px - S1CX, py - S1CY, pz - S1CZ, fmul(S1R, S1R), ldx, ldy, ldz);
        if (t > EPS && t < ld) return true;
        t = sphT(px - S2CX, py - S2CY, pz - S2CZ, fmul(S2R, S2R), ldx, ldy, ldz);
        if (t > EPS && t < ld) return true;
        t = sphT(px - S3CX, py - S3CY, pz - S3CZ, fmul(S3R, S3R), ldx, ldy, ldz);
        if (t > EPS && t < ld) return true;
        return false;
    }

    // ---------- 天空 ----------
    static int sky(int dx, int dy, int dz) {
        int b = 13107;                    // 0.20 基础亮度
        if (dy > 0) b += dy >> 1;         // 越往上越亮
        int sd = dot(dx, dy, dz, sunX, sunY, sunZ);
        if (sd > SUNHOT) return ONE + ONE / 2;   // 太阳圆盘：拉满
        if (sd > SUNWARM) b += 19660;            // 日晕
        return b;
    }

    // ---------- 主追踪 ----------
    // 返回该光线的亮度（fix16，大致 0..1.5）
    static int traceRay(int ox, int oy, int oz, int dx, int dy, int dz, int depth) {
        rays++;
        normalize(dx, dy, dz);
        dx = NV[0]; dy = NV[1]; dz = NV[2];

        sceneHit(ox, oy, oz, dx, dy, dz);
        if (HO < 0) return sky(dx, dy, dz);

        int t = HT;
        int px = ox + fmul(t, dx);
        int py = oy + fmul(t, dy);
        int pz = oz + fmul(t, dz);

        // 法线 + 材质
        int nx, ny, nz, alb;
        boolean sphere;
        if (HO == 4) {
            nx = 0; ny = ONE; nz = 0;
            int fx = px >> 16;            // 算术右移 = floor，负数也正确
            int fz = pz >> 16;
            alb = ((fx + fz) & 1) == 0 ? CHKA : CHKB;
            sphere = false;
        } else {
            int cx, cy, cz, r;
            if (HO == 1)      { cx = S1CX; cy = S1CY; cz = S1CZ; r = S1R; alb = A1; }
            else if (HO == 2) { cx = S2CX; cy = S2CY; cz = S2CZ; r = S2R; alb = A2; }
            else              { cx = S3CX; cy = S3CY; cz = S3CZ; r = S3R; alb = A3; }
            // 掠射命中时定点误差会让命中点略离球面，
            // 归一化法线可保证 |n|=1（反射公式要求）
            normalize(px - cx, py - cy, pz - cz);
            nx = NV[0]; ny = NV[1]; nz = NV[2];
            sphere = true;
        }

        // 指向光源的向量
        int lx = LX - px, ly = LY - py, lz = LZ - pz;
        int ld = fsqrt(dot(lx, ly, lz, lx, ly, lz));
        normalize(lx, ly, lz);
        int lnx = NV[0], lny = NV[1], lnz = NV[2];

        int diff = dot(nx, ny, nz, lnx, lny, lnz);
        if (diff < 0) diff = 0;

        // 阴影（出发点沿法线抬高 EPS 防自相交）
        int sx = px + fmul(EPS, nx);
        int sy = py + fmul(EPS, ny);
        int sz = pz + fmul(EPS, nz);
        boolean shadow = inShadow(sx, sy, sz, lnx, lny, lnz, ld);

        // Phong：环境 + 漫反射
        int bright;
        if (shadow) {
            bright = fmul(alb, AMB);
        } else {
            bright = fmul(alb, AMB + fmul(diff, DIFFK));
        }

        // Blinn 高光（仅球体正面，h = normalize(l - d)）
        // n·d < -0.1 门控：掠射命中的乱法线不打高光，消除轮廓闪点
        if (sphere && !shadow && dot(nx, ny, nz, dx, dy, dz) < -6553) {
            normalize(lnx - dx, lny - dy, lnz - dz);
            int s = dot(nx, ny, nz, NV[0], NV[1], NV[2]);
            if (s > 0) {
                int s2 = fmul(s, s);
                int s4 = fmul(s2, s2);
                int s8 = fmul(s4, s4);
                bright += fmul(s8, SPECK);
            }
        }

        // 一次反射（仅球体，深度 0）
        if (sphere && depth == 0) {
            int dn2 = dot(dx, dy, dz, nx, ny, nz) << 1;
            int rx = dx - fmul(dn2, nx);
            int ry = dy - fmul(dn2, ny);
            int rz = dz - fmul(dn2, nz);
            // 定点误差极端情形下反射方向会坍缩，太短就放弃反射
            int rb = 0;
            if (dot(rx, ry, rz, rx, ry, rz) > 16384) {   // |r| > 0.5（fix16 的 |r|^2）
                rb = traceRay(sx, sy, sz, rx, ry, rz, 1);
            }
            bright = fmul(bright, LOCALK) + fmul(rb, REFLK);
        }

        return bright;
    }

    // ---------- 线程入口 ----------
    public static void run(int band) {
        lazyInit();
        rays = 0;

        Rmiku.IO.printStr("ray worker start, band:");
        Rmiku.IO.printInt(band);

        int y0 = band * RH;
        int y1 = y0 + RH;
        byte[] img = new byte[(W + 1) * RH];
        int idx = 0;

        for (int y = y0; y < y1; y++) {
            int ny = ((H - 1 - 2 * y) * 65536) / H;
            for (int x = 0; x < W; x++) {
                int nx = ((2 * x + 1 - W) * 65536) / W;
                int dx = fmul(nx, TANX);
                int dy = fmul(ny, TANY) - PITCH;
                int b = traceRay(OX, OY, OZ, dx, dy, FOCAL, 0);
                int lvl = (b * 10) >> 16;
                if (lvl < 0) lvl = 0;
                if (lvl > 9) lvl = 9;
                img[idx++] = grad[lvl];
            }
            img[idx++] = '\n';
        }

        String name = band == 0 ? "/tmp/ray_band0.txt" : "/tmp/ray_band1.txt";
        int fd = Rmiku.IO.create(name);
        if (fd < 0) {
            Rmiku.IO.printStr("band file create failed");
            return;
        }
        Rmiku.IO.write(fd, img, img.length);
        Rmiku.IO.close(fd);

        Rmiku.IO.printStr("ray worker done, band:");
        Rmiku.IO.printInt(band);
        Rmiku.IO.printStr("rays traced:");
        Rmiku.IO.printInt(rays);
    }
}