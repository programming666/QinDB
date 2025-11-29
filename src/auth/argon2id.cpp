#include "qindb/argon2id.h"
#include <QString>
#include <QStringList>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <cstring>
#include <algorithm>

namespace qindb {

// ==================== Blake2b实现 ====================

// Blake2b初始化向量
const uint64_t Argon2id::Blake2b::IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

// Blake2b置换表
const uint8_t Argon2id::Blake2b::SIGMA[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}
};

uint64_t Argon2id::Blake2b::rotr64(uint64_t w, unsigned c) {
    return (w >> c) | (w << (64 - c));
}

void Argon2id::Blake2b::G(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d, 
                          uint64_t x, uint64_t y) {
    a = a + b + x;
    d = rotr64(d ^ a, 32);
    c = c + d;
    b = rotr64(b ^ c, 24);
    a = a + b + y;
    d = rotr64(d ^ a, 16);
    c = c + d;
    b = rotr64(b ^ c, 63);
}

void Argon2id::Blake2b::compress(Context& ctx, const uint8_t* block) {
    uint64_t m[16];
    uint64_t v[16];

    // 加载消息块
    for (size_t i = 0; i < 16; i++) {
        m[i] = Argon2id::load64(block + i * 8);
    }

    // 初始化工作向量
    for (size_t i = 0; i < 8; i++) {
        v[i] = ctx.h[i];
    }
    for (size_t i = 0; i < 8; i++) {
        v[i + 8] = IV[i];
    }

    v[12] ^= ctx.t[0];
    v[13] ^= ctx.t[1];
    v[14] ^= ctx.f[0];
    v[15] ^= ctx.f[1];

    // 12轮混合
    for (size_t i = 0; i < 12; i++) {
        G(v[0], v[4], v[8],  v[12], m[SIGMA[i][0]],  m[SIGMA[i][1]]);
        G(v[1], v[5], v[9],  v[13], m[SIGMA[i][2]],  m[SIGMA[i][3]]);
        G(v[2], v[6], v[10], v[14], m[SIGMA[i][4]],  m[SIGMA[i][5]]);
        G(v[3], v[7], v[11], v[15], m[SIGMA[i][6]],  m[SIGMA[i][7]]);
        G(v[0], v[5], v[10], v[15], m[SIGMA[i][8]],  m[SIGMA[i][9]]);
        G(v[1], v[6], v[11], v[12], m[SIGMA[i][10]], m[SIGMA[i][11]]);
        G(v[2], v[7], v[8],  v[13], m[SIGMA[i][12]], m[SIGMA[i][13]]);
        G(v[3], v[4], v[9],  v[14], m[SIGMA[i][14]], m[SIGMA[i][15]]);
    }

    // 更新状态
    for (size_t i = 0; i < 8; i++) {
        ctx.h[i] ^= v[i] ^ v[i + 8];
    }
}

void Argon2id::Blake2b::init(Context& ctx, uint32_t outlen, 
                            const uint8_t* key, size_t keylen) {
    memset(&ctx, 0, sizeof(Context));
    ctx.outlen = outlen;

    // 初始化状态
    for (size_t i = 0; i < 8; i++) {
        ctx.h[i] = IV[i];
    }

    // 参数块
    ctx.h[0] ^= 0x01010000 ^ (keylen << 8) ^ outlen;

    // 如果有密钥，处理密钥块
    if (keylen > 0) {
        uint8_t block[BLAKE2B_BLOCKBYTES];
        memset(block, 0, BLAKE2B_BLOCKBYTES);
        memcpy(block, key, keylen);
        update(ctx, block, BLAKE2B_BLOCKBYTES);
        memset(block, 0, BLAKE2B_BLOCKBYTES);
    }
}

void Argon2id::Blake2b::update(Context& ctx, const uint8_t* in, size_t inlen) {
    if (inlen == 0) return;

    size_t left = ctx.buflen;
    size_t fill = BLAKE2B_BLOCKBYTES - left;

    if (inlen > fill) {
        ctx.buflen = 0;
        // 填充缓冲区并压缩
        memcpy(ctx.buf + left, in, fill);
        ctx.t[0] += BLAKE2B_BLOCKBYTES;
        if (ctx.t[0] < BLAKE2B_BLOCKBYTES) {
            ctx.t[1]++;
        }
        compress(ctx, ctx.buf);
        in += fill;
        inlen -= fill;

        // 压缩完整块
        while (inlen > BLAKE2B_BLOCKBYTES) {
            ctx.t[0] += BLAKE2B_BLOCKBYTES;
            if (ctx.t[0] < BLAKE2B_BLOCKBYTES) {
                ctx.t[1]++;
            }
            compress(ctx, in);
            in += BLAKE2B_BLOCKBYTES;
            inlen -= BLAKE2B_BLOCKBYTES;
        }
    }

    // 存储剩余数据
    memcpy(ctx.buf + ctx.buflen, in, inlen);
    ctx.buflen += inlen;
}

void Argon2id::Blake2b::final(Context& ctx, uint8_t* out) {
    // 填充最后一块
    ctx.t[0] += ctx.buflen;
    if (ctx.t[0] < ctx.buflen) {
        ctx.t[1]++;
    }

    ctx.f[0] = 0xFFFFFFFFFFFFFFFFULL;
    memset(ctx.buf + ctx.buflen, 0, BLAKE2B_BLOCKBYTES - ctx.buflen);
    compress(ctx, ctx.buf);

    // 输出哈希
    for (size_t i = 0; i < ctx.outlen; i++) {
        out[i] = (ctx.h[i / 8] >> (8 * (i % 8))) & 0xFF;
    }
}

QByteArray Argon2id::Blake2b::hash(const QByteArray& data, uint32_t outlen) {
    Context ctx;
    init(ctx, outlen);
    update(ctx, reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    
    QByteArray result(outlen, 0);
    final(ctx, reinterpret_cast<uint8_t*>(result.data()));
    return result;
}

QByteArray Argon2id::Blake2b::hashWithKey(const QByteArray& data, 
                                         const QByteArray& key, uint32_t outlen) {
    Context ctx;
    init(ctx, outlen, reinterpret_cast<const uint8_t*>(key.constData()), key.size());
    update(ctx, reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    
    QByteArray result(outlen, 0);
    final(ctx, reinterpret_cast<uint8_t*>(result.data()));
    return result;
}

// ==================== Argon2id核心实现 ====================

void Argon2id::store32(uint8_t* dst, uint32_t w) {
    dst[0] = static_cast<uint8_t>(w);
    dst[1] = static_cast<uint8_t>(w >> 8);
    dst[2] = static_cast<uint8_t>(w >> 16);
    dst[3] = static_cast<uint8_t>(w >> 24);
}

void Argon2id::store64(uint8_t* dst, uint64_t w) {
    store32(dst, static_cast<uint32_t>(w));
    store32(dst + 4, static_cast<uint32_t>(w >> 32));
}

uint64_t Argon2id::load64(const uint8_t* src) {
    uint64_t w = 0;
    for (int i = 0; i < 8; i++) {
        w |= static_cast<uint64_t>(src[i]) << (8 * i);
    }
    return w;
}

QByteArray Argon2id::blake2bLong(const QByteArray& input, uint32_t outlen) {
    QByteArray result;
    const uint32_t BLAKE2B_OUTBYTES = 64;
    
    if (outlen <= BLAKE2B_OUTBYTES) {
        return Blake2b::hash(input, outlen);
    }

    // 对于长输出，使用特殊方法
    QByteArray outlenBytes(4, 0);
    store32(reinterpret_cast<uint8_t*>(outlenBytes.data()), outlen);
    
    QByteArray toHash = outlenBytes + input;
    QByteArray outBuffer = Blake2b::hash(toHash, BLAKE2B_OUTBYTES);
    
    result = outBuffer.left(BLAKE2B_OUTBYTES / 2);
    uint32_t remaining = outlen - BLAKE2B_OUTBYTES / 2;
    
    while (remaining > BLAKE2B_OUTBYTES) {
        outBuffer = Blake2b::hash(outBuffer, BLAKE2B_OUTBYTES);
        result += outBuffer.left(BLAKE2B_OUTBYTES / 2);
        remaining -= BLAKE2B_OUTBYTES / 2;
    }
    
    if (remaining > 0) {
        outBuffer = Blake2b::hash(outBuffer, remaining);
        result += outBuffer;
    }
    
    return result;
}

void Argon2id::initialize(Context& ctx, const QByteArray& password, 
                         const QByteArray& salt, const Parameters& params) {
    ctx.params = params;
    ctx.lanes = params.parallelism;
    ctx.timeCost = params.timeCost;
    
    // 计算内存块数量
    uint32_t memoryBlocks = params.memoryCost;
    if (memoryBlocks < 8 * params.parallelism) {
        memoryBlocks = 8 * params.parallelism;
    }
    
    ctx.laneLength = memoryBlocks / params.parallelism;
    ctx.segmentLength = ctx.laneLength / 4;
    
    // 分配内存
    ctx.memory.resize(ctx.lanes * ctx.laneLength);
}

void Argon2id::fillFirstBlocks(Context& ctx, const QByteArray& password, 
                              const QByteArray& salt) {
    // 构建初始哈希H0
    QByteArray h0;
    h0.resize(4 * 10 + password.size() + salt.size());
    
    uint8_t* p = reinterpret_cast<uint8_t*>(h0.data());
    store32(p, ctx.params.parallelism); p += 4;
    store32(p, ctx.params.hashLength); p += 4;
    store32(p, ctx.params.memoryCost); p += 4;
    store32(p, ctx.params.timeCost); p += 4;
    store32(p, ARGON2_VERSION); p += 4;
    store32(p, ARGON2_ID); p += 4;
    store32(p, password.size()); p += 4;
    memcpy(p, password.constData(), password.size()); p += password.size();
    store32(p, salt.size()); p += 4;
    memcpy(p, salt.constData(), salt.size()); p += salt.size();
    store32(p, 0); p += 4;  // 密钥长度
    store32(p, 0);          // 关联数据长度
    
    QByteArray h0Hash = Blake2b::hash(h0, 64);
    
    // 填充每个通道的前两个块
    for (uint32_t lane = 0; lane < ctx.lanes; lane++) {
        // 块0
        QByteArray h0Extended(72, 0);
        memcpy(h0Extended.data(), h0Hash.constData(), 64);
        store32(reinterpret_cast<uint8_t*>(h0Extended.data()) + 64, 0);
        store32(reinterpret_cast<uint8_t*>(h0Extended.data()) + 68, lane);
        
        QByteArray block0Data = blake2bLong(h0Extended, BLOCK_SIZE);
        memcpy(ctx.memory[lane * ctx.laneLength].v, block0Data.constData(), BLOCK_SIZE);
        
        // 块1
        store32(reinterpret_cast<uint8_t*>(h0Extended.data()) + 64, 1);
        QByteArray block1Data = blake2bLong(h0Extended, BLOCK_SIZE);
        memcpy(ctx.memory[lane * ctx.laneLength + 1].v, block1Data.constData(), BLOCK_SIZE);
    }
}

void Argon2id::copyBlock(Block& dst, const Block& src) {
    memcpy(dst.v, src.v, sizeof(dst.v));
}

void Argon2id::xorBlock(Block& dst, const Block& src) {
    for (uint32_t i = 0; i < QWORDS_IN_BLOCK; i++) {
        dst.v[i] ^= src.v[i];
    }
}

void Argon2id::permute(Block& block) {
    // Blake2b轮函数的简化版本
    for (uint32_t i = 0; i < 8; i++) {
        // 列混合
        for (uint32_t j = 0; j < 8; j++) {
            uint64_t a = block.v[j];
            uint64_t b = block.v[j + 16];
            uint64_t c = block.v[j + 32];
            uint64_t d = block.v[j + 48];
            
            a = a + b; d ^= a; d = (d >> 32) | (d << 32);
            c = c + d; b ^= c; b = (b >> 24) | (b << 40);
            a = a + b; d ^= a; d = (d >> 16) | (d << 48);
            c = c + d; b ^= c; b = (b >> 63) | (b << 1);
            
            block.v[j] = a;
            block.v[j + 16] = b;
            block.v[j + 32] = c;
            block.v[j + 48] = d;
        }
        
        // 对角线混合
        for (uint32_t j = 0; j < 8; j++) {
            uint32_t idx1 = j;
            uint32_t idx2 = ((j + 1) % 8) + 16;
            uint32_t idx3 = ((j + 2) % 8) + 32;
            uint32_t idx4 = ((j + 3) % 8) + 48;
            
            uint64_t a = block.v[idx1];
            uint64_t b = block.v[idx2];
            uint64_t c = block.v[idx3];
            uint64_t d = block.v[idx4];
            
            a = a + b; d ^= a; d = (d >> 32) | (d << 32);
            c = c + d; b ^= c; b = (b >> 24) | (b << 40);
            a = a + b; d ^= a; d = (d >> 16) | (d << 48);
            c = c + d; b ^= c; b = (b >> 63) | (b << 1);
            
            block.v[idx1] = a;
            block.v[idx2] = b;
            block.v[idx3] = c;
            block.v[idx4] = d;
        }
    }
}

void Argon2id::fillBlock(const Block& prev, const Block& ref, Block& next, bool withXor) {
    Block R, Z;
    
    // R = prev XOR ref
    copyBlock(R, prev);
    xorBlock(R, ref);
    copyBlock(Z, R);
    
    // 应用置换
    permute(Z);
    
    // next = R XOR Z
    if (withXor) {
        xorBlock(next, R);
        xorBlock(next, Z);
    } else {
        copyBlock(next, R);
        xorBlock(next, Z);
    }
}

uint32_t Argon2id::indexAlpha(Context& ctx, uint32_t pass, uint32_t slice,
                              uint32_t lane, uint32_t index, uint64_t pseudoRand) {
    uint32_t refLane = lane;
    uint32_t refIndex;
    
    // Argon2id: 前半部分使用Argon2i，后半部分使用Argon2d
    bool dataDependent = (pass != 0) || (slice >= 2);
    
    if (pass == 0 && slice == 0) {
        refLane = static_cast<uint32_t>(pseudoRand >> 32) % ctx.lanes;
    }
    
    uint32_t refAreaSize;
    if (pass == 0) {
        if (slice == 0) {
            refAreaSize = index - 1;
        } else {
            refAreaSize = slice * ctx.segmentLength + index - 1;
        }
    } else {
        refAreaSize = ctx.laneLength - ctx.segmentLength + index - 1;
    }
    
    if (refAreaSize == 0) {
        refAreaSize = 1;
    }
    
    uint64_t relativePos = pseudoRand & 0xFFFFFFFF;
    relativePos = (relativePos * relativePos) >> 32;
    relativePos = refAreaSize - 1 - ((refAreaSize * relativePos) >> 32);
    
    uint32_t startPos = 0;
    if (pass != 0) {
        startPos = ((slice + 1) * ctx.segmentLength) % ctx.laneLength;
    }
    
    refIndex = (startPos + static_cast<uint32_t>(relativePos)) % ctx.laneLength;
    
    return refLane * ctx.laneLength + refIndex;
}

void Argon2id::fillSegment(Context& ctx, uint32_t pass, uint32_t lane, uint32_t slice) {
    uint32_t startIndex = 0;
    if (pass == 0 && slice == 0) {
        startIndex = 2;
    }
    
    uint32_t currOffset = lane * ctx.laneLength + slice * ctx.segmentLength + startIndex;
    
    for (uint32_t i = startIndex; i < ctx.segmentLength; i++) {
        uint32_t prevOffset = currOffset - 1;
        if (i == 0 && slice == 0) {
            prevOffset = lane * ctx.laneLength + ctx.laneLength - 1;
        }
        
        // 生成伪随机索引
        uint64_t pseudoRand = ctx.memory[prevOffset].v[0];
        uint32_t refIndex = indexAlpha(ctx, pass, slice, lane, i, pseudoRand);
        
        // 填充块
        bool withXor = (pass != 0);
        fillBlock(ctx.memory[prevOffset], ctx.memory[refIndex], 
                 ctx.memory[currOffset], withXor);
        
        currOffset++;
    }
}

void Argon2id::fillMemoryBlocks(Context& ctx) {
    for (uint32_t pass = 0; pass < ctx.timeCost; pass++) {
        for (uint32_t slice = 0; slice < 4; slice++) {
            for (uint32_t lane = 0; lane < ctx.lanes; lane++) {
                fillSegment(ctx, pass, lane, slice);
            }
        }
    }
}

QByteArray Argon2id::finalize(Context& ctx, uint32_t hashLength) {
    // XOR所有通道的最后一个块
    Block finalBlock;
    copyBlock(finalBlock, ctx.memory[ctx.laneLength - 1]);
    
    for (uint32_t lane = 1; lane < ctx.lanes; lane++) {
        xorBlock(finalBlock, ctx.memory[lane * ctx.laneLength + ctx.laneLength - 1]);
    }
    
    // 使用Blake2b生成最终哈希
    QByteArray finalBlockData(BLOCK_SIZE, 0);
    memcpy(finalBlockData.data(), finalBlock.v, BLOCK_SIZE);
    
    return blake2bLong(finalBlockData, hashLength);
}

QByteArray Argon2id::hash(const QByteArray& password, const QByteArray& salt,
                         const Parameters& params) {
    Context ctx;
    
    // 初始化
    initialize(ctx, password, salt, params);
    
    // 填充前两个块
    fillFirstBlocks(ctx, password, salt);
    
    // 填充剩余内存块
    fillMemoryBlocks(ctx);
    
    // 生成最终哈希
    return finalize(ctx, params.hashLength);
}

QString Argon2id::hashEncoded(const QByteArray& password, const QByteArray& salt,
                              const Parameters& params) {
    QByteArray hashResult = hash(password, salt, params);
    
    // 格式: $argon2id$v=19$m=65536,t=3,p=1$salt$hash
    QString encoded = QString("$argon2id$v=%1$m=%2,t=%3,p=%4$%5$%6")
        .arg(ARGON2_VERSION)
        .arg(params.memoryCost)
        .arg(params.timeCost)
        .arg(params.parallelism)
        .arg(QString::fromLatin1(salt.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
        .arg(QString::fromLatin1(hashResult.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    
    return encoded;
}

bool Argon2id::parseEncoded(const QString& encoded, Parameters& params,
                           QByteArray& salt, QByteArray& hash) {
    QStringList parts = encoded.split('$', Qt::SkipEmptyParts);
    
    if (parts.size() != 5) {
        return false;
    }
    
    if (parts[0] != "argon2id") {
        return false;
    }
    
    // 解析版本
    if (!parts[1].startsWith("v=")) {
        return false;
    }
    
    // 解析参数
    QStringList paramParts = parts[2].split(',');
    for (const QString& param : paramParts) {
        QStringList kv = param.split('=');
        if (kv.size() != 2) {
            return false;
        }
        
        if (kv[0] == "m") {
            params.memoryCost = kv[1].toUInt();
        } else if (kv[0] == "t") {
            params.timeCost = kv[1].toUInt();
        } else if (kv[0] == "p") {
            params.parallelism = kv[1].toUInt();
        }
    }
    
    // 解析盐值和哈希
    salt = QByteArray::fromBase64(parts[3].toLatin1(), 
                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    hash = QByteArray::fromBase64(parts[4].toLatin1(),
                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    
    params.saltLength = salt.size();
    params.hashLength = hash.size();
    
    return true;
}

bool Argon2id::verify(const QByteArray& password, const QString& encodedHash) {
    Parameters params;
    QByteArray salt, expectedHash;
    
    if (!parseEncoded(encodedHash, params, salt, expectedHash)) {
        return false;
    }
    
    QByteArray computedHash = hash(password, salt, params);
    
    // 常量时间比较
    if (computedHash.size() != expectedHash.size()) {
        return false;
    }
    
    int result = 0;
    for (int i = 0; i < computedHash.size(); i++) {
        result |= static_cast<uint8_t>(computedHash[i]) ^ static_cast<uint8_t>(expectedHash[i]);
    }
    
    return result == 0;
}

} // namespace qindb