#ifndef QINDB_ARGON2ID_H
#define QINDB_ARGON2ID_H

#include <QByteArray>
#include <cstdint>
#include <vector>

namespace qindb {

/**
 * @brief Argon2id密码哈希算法实现
 *
 * Argon2id是Argon2的混合版本，结合了Argon2i和Argon2d的优点。
 *
 * 参数说明:
 * - timeCost: 迭代次数(默认3)
 * - memoryCost: 内存使用量，单位KB(默认65536 = 64MB)
 * - parallelism: 并行度(默认1)
 * - hashLength: 输出哈希长度(默认32字节)
 * - saltLength: 盐值长度(默认16字节)
 */
class Argon2id {
public:
    /**
     * @brief Argon2id参数配置
     */
    struct Parameters {
        uint32_t timeCost;      // 迭代次数（t_cost）
        uint32_t memoryCost;    // 内存大小，单位KB（m_cost）
        uint32_t parallelism;   // 并行度（lanes）
        uint32_t hashLength;    // 输出哈希长度
        uint32_t saltLength;    // 盐值长度
        
        // 默认参数（OWASP推荐）
        Parameters()
            : timeCost(3)
            , memoryCost(65536)  // 64 MB
            , parallelism(1)
            , hashLength(32)
            , saltLength(16)
        {}
    };

    /**
     * @brief 使用Argon2id算法哈希密码
     * @param password 明文密码
     * @param salt 盐值
     * @param params Argon2参数
     * @return 哈希结果
     */
    static QByteArray hash(const QByteArray& password, 
                          const QByteArray& salt,
                          const Parameters& params = Parameters());

    /**
     * @brief 使用Argon2id算法哈希密码（带编码输出）
     * @param password 明文密码
     * @param salt 盐值
     * @param params Argon2参数
     * @return 编码后的哈希字符串（包含参数和盐值）
     * 格式: $argon2id$v=19$m=65536,t=3,p=1$salt$hash
     */
    static QString hashEncoded(const QByteArray& password,
                              const QByteArray& salt,
                              const Parameters& params = Parameters());

    /**
     * @brief 验证密码
     * @param password 明文密码
     * @param encodedHash 编码的哈希字符串
     * @return 是否匹配
     */
    static bool verify(const QByteArray& password, const QString& encodedHash);

private:
    // Argon2版本号
    static constexpr uint32_t ARGON2_VERSION = 0x13;  // 版本19
    
    // 块大小（1024字节 = 128个uint64_t）
    static constexpr uint32_t BLOCK_SIZE = 1024;
    static constexpr uint32_t QWORDS_IN_BLOCK = BLOCK_SIZE / 8;
    
    // Argon2类型
    static constexpr uint32_t ARGON2_ID = 2;

    /**
     * @brief Blake2b哈希函数（Argon2的核心哈希函数）
     */
    class Blake2b {
    public:
        static QByteArray hash(const QByteArray& data, uint32_t outlen);
        static QByteArray hashWithKey(const QByteArray& data, const QByteArray& key, uint32_t outlen);
        
    private:
        static constexpr uint32_t BLAKE2B_BLOCKBYTES = 128;
        static constexpr uint32_t BLAKE2B_OUTBYTES = 64;
        
        struct Context {
            uint64_t h[8];
            uint64_t t[2];
            uint64_t f[2];
            uint8_t buf[BLAKE2B_BLOCKBYTES];
            size_t buflen;
            size_t outlen;
        };
        
        static void init(Context& ctx, uint32_t outlen, const uint8_t* key = nullptr, size_t keylen = 0);
        static void update(Context& ctx, const uint8_t* in, size_t inlen);
        static void final(Context& ctx, uint8_t* out);
        static void compress(Context& ctx, const uint8_t* block);
        
        static uint64_t rotr64(uint64_t w, unsigned c);
        static void G(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d, uint64_t x, uint64_t y);
        
        static const uint64_t IV[8];
        static const uint8_t SIGMA[12][16];
    };

    /**
     * @brief 内存块结构
     */
    struct Block {
        uint64_t v[QWORDS_IN_BLOCK];
        
        Block() { clear(); }
        void clear() { memset(v, 0, sizeof(v)); }
    };

    /**
     * @brief Argon2上下文
     */
    struct Context {
        std::vector<Block> memory;  // 内存块数组
        uint32_t lanes;             // 并行通道数
        uint32_t segmentLength;     // 每个段的长度
        uint32_t laneLength;        // 每个通道的长度
        uint32_t timeCost;          // 时间成本
        Parameters params;          // 参数
    };

    /**
     * @brief 初始化Argon2上下文
     */
    static void initialize(Context& ctx, const QByteArray& password, 
                          const QByteArray& salt, const Parameters& params);

    /**
     * @brief 填充第一个和第二个块
     */
    static void fillFirstBlocks(Context& ctx, const QByteArray& password, const QByteArray& salt);

    /**
     * @brief 执行Argon2id的内存填充过程
     */
    static void fillMemoryBlocks(Context& ctx);

    /**
     * @brief 填充一个段
     */
    static void fillSegment(Context& ctx, uint32_t pass, uint32_t lane, uint32_t slice);

    /**
     * @brief 计算块索引
     */
    static uint32_t indexAlpha(Context& ctx, uint32_t pass, uint32_t slice, 
                              uint32_t lane, uint32_t index, uint64_t pseudoRand);

    /**
     * @brief 块混合函数（G函数）
     */
    static void fillBlock(const Block& prev, const Block& ref, Block& next, bool withXor);

    /**
     * @brief Blake2b长哈希
     */
    static QByteArray blake2bLong(const QByteArray& input, uint32_t outlen);

    /**
     * @brief 最终化，提取哈希结果
     */
    static QByteArray finalize(Context& ctx, uint32_t hashLength);

    /**
     * @brief XOR两个块
     */
    static void xorBlock(Block& dst, const Block& src);

    /**
     * @brief 复制块
     */
    static void copyBlock(Block& dst, const Block& src);

    /**
     * @brief 置换函数P
     */
    static void permute(Block& block);

    /**
     * @brief 从编码字符串解析参数
     */
    static bool parseEncoded(const QString& encoded, Parameters& params, 
                            QByteArray& salt, QByteArray& hash);

    /**
     * @brief 将uint32转为小端字节序
     */
    static void store32(uint8_t* dst, uint32_t w);
    
    /**
     * @brief 将uint64转为小端字节序
     */
    static void store64(uint8_t* dst, uint64_t w);
    
    /**
     * @brief 从小端字节序加载uint64
     */
    static uint64_t load64(const uint8_t* src);
};

} // namespace qindb

#endif // QINDB_ARGON2ID_H