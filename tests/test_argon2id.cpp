#include "qindb/argon2id.h"
#include "qindb/password_hasher.h"
#include <QCoreApplication>
#include <QDebug>
#include <QCryptographicHash>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace qindb;

// 全局输出流
QTextStream cout(stdout);

// 控制台输出辅助函数
void printLine(const QString& text) {
    cout << text << Qt::endl;
    cout.flush();
}

int testsPassed = 0;
int testsFailed = 0;

#define TEST(name) \
    printLine("\n[测试] " + QString(name)); \
    if (true)

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        printLine("  ❌ 断言失败: " + QString(#condition)); \
        testsFailed++; \
        return false; \
    }

#define ASSERT_FALSE(condition) \
    if (condition) { \
        printLine("  ❌ 断言失败: 期望为假: " + QString(#condition)); \
        testsFailed++; \
        return false; \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        printLine("  ❌ 断言失败: " + QString(#a) + " != " + QString(#b)); \
        printLine("     实际值: " + QString::number(a) + " vs " + QString::number(b)); \
        testsFailed++; \
        return false; \
    }

// 测试Argon2id基本哈希功能
bool test_argon2id_basic_hash() {
    TEST("Argon2id基本哈希功能");
    
    QByteArray password = "TestPassword123!";
    QByteArray salt(16, 0);
    
    // 填充固定的盐值以便测试
    for (int i = 0; i < 16; i++) {
        salt[i] = static_cast<char>(i);
    }
    
    Argon2id::Parameters params;
    params.memoryCost = 16384;  // 1MB（测试用较小值）
    params.timeCost = 1;
    params.parallelism = 1;
    params.hashLength = 256;
    
    QByteArray hash = Argon2id::hash(password, salt, params);
    
    ASSERT_EQ(hash.size(), 256);
    printLine("  ✓ 哈希长度正确: " + QString::number(hash.size()) + " 字节");
    printLine("  ✓ 哈希值: " + QString(hash.toHex()));
    
    testsPassed++;
    return true;
}

// 测试Argon2id编码格式
bool test_argon2id_encoded() {
    TEST("Argon2id编码格式");
    
    QByteArray password = "MySecurePassword";
    QByteArray salt(16, 0);
    
    for (int i = 0; i < 16; i++) {
        salt[i] = static_cast<char>(i * 2);
    }
    
    printLine("  原始密码: " + QString(password));
    
    Argon2id::Parameters params;
    params.memoryCost = 1024;
    params.timeCost = 2;
    params.parallelism = 1;
    params.hashLength = 32;
    
    QString encoded = Argon2id::hashEncoded(password, salt, params);
    
    ASSERT_TRUE(encoded.startsWith("$argon2id$"));
    ASSERT_TRUE(encoded.contains("$v=19$"));
    ASSERT_TRUE(encoded.contains("$m=1024,t=2,p=1$"));
    
    printLine("  ✓ 编码格式正确");
    printLine("  ✓ 完整编码: " + encoded);
    
    testsPassed++;
    return true;
}

// 测试Argon2id验证功能
bool test_argon2id_verify() {
    TEST("Argon2id验证功能");
    
    QByteArray password = "CorrectPassword";
    QByteArray wrongPassword = "WrongPassword";
    QByteArray salt(16, 0);
    
    for (int i = 0; i < 16; i++) {
        salt[i] = static_cast<char>(i + 10);
    }
    
    printLine("  正确密码: " + QString(password));
    printLine("  错误密码: " + QString(wrongPassword));
    
    Argon2id::Parameters params;
    params.memoryCost = 1024;
    params.timeCost = 1;
    params.parallelism = 1;
    params.hashLength = 32;
    
    QString encoded = Argon2id::hashEncoded(password, salt, params);
    printLine("  生成的哈希: " + encoded);
    
    // 正确密码应该验证成功
    ASSERT_TRUE(Argon2id::verify(password, encoded));
    printLine("  ✓ 正确密码验证成功");
    
    // 错误密码应该验证失败
    ASSERT_FALSE(Argon2id::verify(wrongPassword, encoded));
    printLine("  ✓ 错误密码验证失败");
    
    testsPassed++;
    return true;
}

// 测试PasswordHasher使用Argon2id
bool test_password_hasher_argon2id() {
    TEST("PasswordHasher Argon2id集成");
    
    QString password = "TestPassword123!@#";
    
    printLine("  原始密码: " + password);
    
    // 生成哈希
    QString hash = PasswordHasher::hashPassword(password);
    
    ASSERT_TRUE(hash.startsWith("$argon2id$"));
    printLine("  ✓ 生成的哈希格式正确");
    printLine("  ✓ 完整哈希: " + hash);
    
    // 验证正确密码
    ASSERT_TRUE(PasswordHasher::verifyPassword(password, hash));
    printLine("  ✓ 正确密码 '" + password + "' 验证成功");
    
    // 验证错误密码
    QString wrongPassword = "WrongPassword";
    ASSERT_FALSE(PasswordHasher::verifyPassword(wrongPassword, hash));
    printLine("  ✓ 错误密码 '" + wrongPassword + "' 验证失败");
    
    testsPassed++;
    return true;
}

// 测试向后兼容性（旧SHA-256格式）
bool test_backward_compatibility() {
    TEST("向后兼容性（SHA-256格式）");
    
    QString password = "OldPassword";
    QByteArray passwordBytes = password.toUtf8();
    
    printLine("  旧格式密码: " + password);
    
    // 生成盐值
    QByteArray salt(16, 0);
    for (int i = 0; i < 16; i++) {
        salt[i] = static_cast<char>(i * 3);
    }
    
    // 计算SHA-256哈希
    QByteArray combined = passwordBytes + salt;
    QByteArray hash = QCryptographicHash::hash(combined, QCryptographicHash::Sha256);
    
    // 组合并编码为Base64
    QByteArray stored = hash + salt;
    QString oldFormatHash = QString::fromLatin1(stored.toBase64());
    
    printLine("  旧格式哈希(Base64): " + oldFormatHash);
    printLine("  SHA-256哈希(hex): " + QString(hash.toHex()));
    
    // 验证旧格式仍然可以工作
    ASSERT_TRUE(PasswordHasher::verifyPassword(password, oldFormatHash));
    printLine("  ✓ 旧格式密码 '" + password + "' 验证成功");
    
    ASSERT_FALSE(PasswordHasher::verifyPassword("WrongPassword", oldFormatHash));
    printLine("  ✓ 旧格式错误密码验证失败");
    
    testsPassed++;
    return true;
}

// 测试不同参数的Argon2id
bool test_argon2id_different_params() {
    TEST("不同参数的Argon2id");
    
    QString password = "TestPassword";
    printLine("  测试密码: " + password);
    
    // 测试不同的内存成本
    printLine("\n  配置1: 内存2MB, 迭代2次");
    QString hash1 = PasswordHasher::hashPasswordWithParams(password, 2048, 2, 1);
    printLine("  哈希1: " + hash1);
    
    printLine("\n  配置2: 内存4MB, 迭代2次");
    QString hash2 = PasswordHasher::hashPasswordWithParams(password, 4096, 2, 1);
    printLine("  哈希2: " + hash2);
    
    ASSERT_TRUE(hash1.contains("$m=2048,"));
    ASSERT_TRUE(hash2.contains("$m=4096,"));
    printLine("\n  ✓ 参数正确编码");
    
    // 两个哈希应该不同（因为盐值不同）
    ASSERT_TRUE(hash1 != hash2);
    printLine("  ✓ 不同盐值生成不同哈希");
    
    // 但都应该能验证原密码
    ASSERT_TRUE(PasswordHasher::verifyPassword(password, hash1));
    ASSERT_TRUE(PasswordHasher::verifyPassword(password, hash2));
    printLine("  ✓ 两个哈希都能验证原密码 '" + password + "'");
    
    testsPassed++;
    return true;
}

// 测试密码强度检查
bool test_password_strength() {
    TEST("密码强度检查");
    
    ASSERT_FALSE(PasswordHasher::isPasswordStrong("short"));
    printLine("  ✓ 短密码被识别为弱密码");
    
    ASSERT_FALSE(PasswordHasher::isPasswordStrong("onlylowercase"));
    printLine("  ✓ 单一字符类型被识别为弱密码");
    
    ASSERT_TRUE(PasswordHasher::isPasswordStrong("Strong123!"));
    printLine("  ✓ 强密码被正确识别");
    
    ASSERT_TRUE(PasswordHasher::isPasswordStrong("MyP@ssw0rd"));
    printLine("  ✓ 复杂密码被正确识别");
    
    testsPassed++;
    return true;
}

// 测试相同密码生成不同哈希
bool test_different_salts() {
    TEST("相同密码不同盐值");
    
    QString password = "SamePassword";
    printLine("  测试密码: " + password);
    
    printLine("\n  第一次哈希:");
    QString hash1 = PasswordHasher::hashPassword(password);
    printLine("  " + hash1);
    
    printLine("\n  第二次哈希:");
    QString hash2 = PasswordHasher::hashPassword(password);
    printLine("  " + hash2);
    
    // 两个哈希应该不同
    ASSERT_TRUE(hash1 != hash2);
    printLine("\n  ✓ 相同密码 '" + password + "' 生成不同哈希（盐值不同）");
    
    // 但都应该能验证原密码
    ASSERT_TRUE(PasswordHasher::verifyPassword(password, hash1));
    ASSERT_TRUE(PasswordHasher::verifyPassword(password, hash2));
    printLine("  ✓ 两个哈希都能验证原密码");
    
    testsPassed++;
    return true;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
#ifdef Q_OS_WIN
    // 设置Windows控制台为UTF-8模式
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    
    // Qt6中QTextStream默认使用UTF-8
    cout.setEncoding(QStringConverter::Utf8);
    
    printLine("========================================");
    printLine("Argon2id密码哈希测试");
    printLine("========================================");
    
    test_argon2id_basic_hash();
    test_argon2id_encoded();
    test_argon2id_verify();
    test_password_hasher_argon2id();
    test_backward_compatibility();
    test_argon2id_different_params();
    test_password_strength();
    test_different_salts();
    
    printLine("\n========================================");
    printLine("测试结果:");
    printLine("  通过: " + QString::number(testsPassed));
    printLine("  失败: " + QString::number(testsFailed));
    printLine("  总计: " + QString::number(testsPassed + testsFailed));
    
    if (testsFailed == 0) {
        printLine("\n✓ 所有测试通过！");
        printLine("========================================");
        return 0;
    } else {
        printLine("\n✗ 有测试失败");
        printLine("========================================");
        return 1;
    }
}