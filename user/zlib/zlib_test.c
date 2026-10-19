#include "test.h"
#include "zlib.h"

/* ============================================================================
 * zlib_test.c —— zlib 移植的正确性验证（git 的前置）
 *
 * 由 build.py 的 build_zlib() 专门编译, 产物 /tests/zlib_test。
 *
 * 验四层:
 *   1. 版本号           —— 头文件与库是不是同一份
 *   2. 校验和已知值     —— crc32/adler32 是表驱动的, 表算错会静默出错
 *   3. 一次性 API 回环  —— compress2/uncompress, 同时覆盖"可压缩"和"不可压缩"
 *   4. 流式 API 回环    —— git 实际就是这么用的 (deflate/inflate + z_stream)
 *
 * 为什么值得单独测: git 的 object 一旦 zlib 算错, 不会立刻报错 ——
 * 它会把错误的字节写进 .git/objects, 等下次读出来才发现 "incorrect header check",
 * 那时已经很难判断是 zlib 的问题还是 git 的问题。
 * ==========================================================================*/

#define BUF_SIZE (8 * 1024)

static unsigned char src_buf[BUF_SIZE];
static unsigned char comp_buf[BUF_SIZE * 2];
static unsigned char out_buf[BUF_SIZE];

/* 伪随机但可复现（不用 rand(), 避免和 libc 的 rand 状态互相干扰） */
static unsigned int next_rand(unsigned int *s) {
    *s = *s * 1103515245u + 12345u;
    return (*s >> 16) & 0x7fffu;
}

static int bytes_equal(const unsigned char *a, const unsigned char *b, unsigned int n) {
    for (unsigned int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

int main(void) {
    TEST_START("misc_zlib");

    /* ---------- 1. 版本 ---------- */
    const char *ver = zlibVersion();
    CHECK(ver != 0 && ver[0] == '1', "zlibVersion() 形如 1.x");

    /* ---------- 2. 校验和的已知校验值 ---------- */
    /* CRC-32 标准 check: crc32("123456789") == 0xCBF43926
       Adler-32 标准 check: adler32("Wikipedia") == 0x11E60398 */
    uLong crc = crc32(0L, (const Bytef *)"123456789", 9);
    CHECK(crc == 0xCBF43926UL, "crc32(\"123456789\") == 0xCBF43926");
    /* 注意: adler32 的初始值不是 0 —— 这与 crc32 的约定不同, 很容易写错。
       zlib 文档的写法是先 adler32(0, Z_NULL, 0) 取初始值(=1), 再喂数据。
       直接传 0 会得到 0x11DD0397(每轮比标准值少 1), 不是标准 check 值。 */
    uLong adl = adler32(adler32(0L, Z_NULL, 0), (const Bytef *)"Wikipedia", 9);
    CHECK(adl == 0x11E60398UL, "adler32(\"Wikipedia\") == 0x11E60398");

    /* ---------- 3. 一次性 API：可压缩数据回环 ---------- */
    for (int i = 0; i < BUF_SIZE; i++) {
        src_buf[i] = (unsigned char)('A' + (i % 7));
    }
    uLongf clen = sizeof(comp_buf);
    int rc = compress2(comp_buf, &clen, src_buf, BUF_SIZE, Z_BEST_COMPRESSION);
    CHECK(rc == Z_OK, "compress2(可压缩) 返回 Z_OK");
    CHECK(clen < (uLongf)BUF_SIZE, "压缩后长度确实变小");

    uLongf olen = sizeof(out_buf);
    rc = uncompress(out_buf, &olen, comp_buf, clen);
    CHECK(rc == Z_OK, "uncompress 返回 Z_OK");
    CHECK(olen == (uLongf)BUF_SIZE, "解压后长度还原");
    CHECK(bytes_equal(out_buf, src_buf, BUF_SIZE), "解压内容逐字节相同");

    /* ---------- 4. 不可压缩数据也必须能回环 ---------- */
    /* 伪随机数据几乎压不动, 会走 stored block 路径 —— 这也是 git 存二进制
       大文件时的真实情况, 和"全是 A"的用例覆盖的代码完全不同。 */
    unsigned int seed = 12345;
    for (int i = 0; i < BUF_SIZE; i++) {
        src_buf[i] = (unsigned char)next_rand(&seed);
    }
    clen = sizeof(comp_buf);
    rc = compress2(comp_buf, &clen, src_buf, BUF_SIZE, Z_DEFAULT_COMPRESSION);
    CHECK(rc == Z_OK, "compress2(不可压缩) 返回 Z_OK");
    olen = sizeof(out_buf);
    rc = uncompress(out_buf, &olen, comp_buf, clen);
    CHECK(rc == Z_OK && olen == (uLongf)BUF_SIZE, "不可压缩数据回环成功");
    CHECK(bytes_equal(out_buf, src_buf, BUF_SIZE), "不可压缩数据内容一致");

    /* ---------- 5. 流式 API（git 的实际用法）---------- */
    for (int i = 0; i < BUF_SIZE; i++) {
        src_buf[i] = (unsigned char)('a' + (i % 26));
    }

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    CHECK(deflateInit(&zs, Z_DEFAULT_COMPRESSION) == Z_OK, "deflateInit");

    zs.next_in = src_buf;
    zs.avail_in = BUF_SIZE;
    zs.next_out = comp_buf;
    zs.avail_out = sizeof(comp_buf);
    rc = deflate(&zs, Z_FINISH);
    CHECK(rc == Z_STREAM_END, "deflate(Z_FINISH) 返回 Z_STREAM_END");
    uLong stream_len = zs.total_out;
    CHECK(stream_len > 0 && stream_len < (uLong)sizeof(comp_buf), "流式压缩产出长度合理");
    deflateEnd(&zs);

    memset(&zs, 0, sizeof(zs));
    CHECK(inflateInit(&zs) == Z_OK, "inflateInit");

    zs.next_in = comp_buf;
    zs.avail_in = stream_len;
    zs.next_out = out_buf;
    zs.avail_out = sizeof(out_buf);
    rc = inflate(&zs, Z_FINISH);
    CHECK(rc == Z_STREAM_END, "inflate 返回 Z_STREAM_END");
    CHECK(zs.total_out == (uLong)BUF_SIZE, "流式解压长度还原");
    inflateEnd(&zs);

    CHECK(bytes_equal(out_buf, src_buf, BUF_SIZE), "流式解压内容逐字节相同");

    /* ---------- 6. 损坏数据必须被检测出来 ---------- */
    /* 翻转压缩流中间一个字节, inflate 应当拒绝而不是"解出一堆垃圾"。
       git 读 packfile 时依赖这个行为来判断仓库损坏。 */
    comp_buf[stream_len / 2] ^= 0xFF;
    memset(&zs, 0, sizeof(zs));
    inflateInit(&zs);
    zs.next_in = comp_buf;
    zs.avail_in = stream_len;
    zs.next_out = out_buf;
    zs.avail_out = sizeof(out_buf);
    rc = inflate(&zs, Z_FINISH);
    CHECK(rc != Z_OK && rc != Z_STREAM_END, "损坏的压缩流被拒绝");
    inflateEnd(&zs);

    /* ---------- 7. gzFile：文件级 API（.gz 容器 + FILE* I/O）---------- */
    /* 这一段走的是**完全不同的代码路径**：gzip 容器（10 字节头 + CRC32 尾）
       加上 fopen/fread/fwrite。前 6 组都只碰内存。
       顺带验证 zlib 与 gzip 是两种容器：.gz 文件头是 1f 8b，
       而 git 的 object 是 zlib 格式（78 9c）—— 所以 git 其实用不到这一层。 */
    {
        const char *gzpath = "/tmp/zlib_gz_test.gz";

        gzFile g = gzopen(gzpath, "wb");
        CHECK(g != 0, "gzopen(\"wb\") 成功");
        if (g) {
            int n = gzwrite(g, src_buf, BUF_SIZE);
            CHECK(n == BUF_SIZE, "gzwrite 写满全部字节");
            CHECK(gzclose(g) == Z_OK, "gzclose(写) 返回 Z_OK");
        }

        FILE *fp = fopen(gzpath, "rb");
        unsigned char magic[2] = {0, 0};
        if (fp) {
            fread(magic, 1, 2, fp);
            fclose(fp);
        }
        CHECK(magic[0] == 0x1f && magic[1] == 0x8b, "文件头是 gzip magic 1f 8b");

        g = gzopen(gzpath, "rb");
        CHECK(g != 0, "gzopen(\"rb\") 成功");
        if (g) {
            int n = gzread(g, out_buf, BUF_SIZE);
            CHECK(n == BUF_SIZE, "gzread 读回全部字节");
            CHECK(bytes_equal(out_buf, src_buf, BUF_SIZE), "gz 读回内容逐字节相同");
            gzclose(g);
        }

        unlink(gzpath);
    }

    TEST_END();
}
