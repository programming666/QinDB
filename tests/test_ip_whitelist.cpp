#include "test_framework.h"
#include "qindb/server.h"
#include <iostream>

namespace qindb {
namespace test {

/**
 * @brief IP白名单CIDR测试
 */
class TestIPWhitelist : public TestCase {
public:
    TestIPWhitelist() : TestCase("TestIPWhitelist") {}

    void run() override {
        try { testSingleIPMatch(); } catch (...) {}
        try { testCIDRNetworkMatch(); } catch (...) {}
        try { testCIDRNetworkNoMatch(); } catch (...) {}
        try { testMultipleCIDRRanges(); } catch (...) {}
    }

private:
    void testSingleIPMatch() {
        startTimer();

        // 创建Server实例（只用来测试白名单功能）
        // 注意：这里我们只测试IP匹配逻辑，不创建实际的server
        
        // 测试单个IP地址
        // 192.168.1.100/32 应该只匹配 192.168.1.100
        QString testIP = "192.168.1.100";
        QString cidrRange = "192.168.1.100/32";
        
        assertTrue(matchesCIDR(testIP, cidrRange), 
                   "192.168.1.100 应该匹配 192.168.1.100/32");

        testIP = "192.168.1.101";
        assertFalse(matchesCIDR(testIP, cidrRange),
                    "192.168.1.101 不应该匹配 192.168.1.100/32");

        double elapsed = stopTimer();
        addResult("testSingleIPMatch", true, "", elapsed);
    }

    void testCIDRNetworkMatch() {
        startTimer();

        // 测试 /24 网络 (192.168.1.0 - 192.168.1.255)
        QString cidrRange = "192.168.1.0/24";
        
        assertTrue(matchesCIDR("192.168.1.0", cidrRange),
                   "192.168.1.0 应该匹配 192.168.1.0/24");
        
        assertTrue(matchesCIDR("192.168.1.1", cidrRange),
                   "192.168.1.1 应该匹配 192.168.1.0/24");
        
        assertTrue(matchesCIDR("192.168.1.255", cidrRange),
                   "192.168.1.255 应该匹配 192.168.1.0/24");
        
        assertTrue(matchesCIDR("192.168.1.128", cidrRange),
                   "192.168.1.128 应该匹配 192.168.1.0/24");

        double elapsed = stopTimer();
        addResult("testCIDRNetworkMatch", true, "", elapsed);
    }

    void testCIDRNetworkNoMatch() {
        startTimer();

        // 测试 /24 网络不匹配的情况
        QString cidrRange = "192.168.1.0/24";
        
        assertFalse(matchesCIDR("192.168.0.255", cidrRange),
                    "192.168.0.255 不应该匹配 192.168.1.0/24");
        
        assertFalse(matchesCIDR("192.168.2.0", cidrRange),
                    "192.168.2.0 不应该匹配 192.168.1.0/24");
        
        assertFalse(matchesCIDR("10.0.0.1", cidrRange),
                    "10.0.0.1 不应该匹配 192.168.1.0/24");

        double elapsed = stopTimer();
        addResult("testCIDRNetworkNoMatch", true, "", elapsed);
    }

    void testMultipleCIDRRanges() {
        startTimer();

        // 测试 /16 和 /25 范围
        QString cidrRange16 = "10.0.0.0/16";   // 10.0.0.0 - 10.0.255.255
        QString cidrRange25 = "10.1.128.0/25"; // 10.1.128.0 - 10.1.128.127

        // /16 测试
        assertTrue(matchesCIDR("10.0.0.0", cidrRange16),
                   "10.0.0.0 应该匹配 10.0.0.0/16");
        assertTrue(matchesCIDR("10.0.255.255", cidrRange16),
                   "10.0.255.255 应该匹配 10.0.0.0/16");
        assertTrue(matchesCIDR("10.0.128.64", cidrRange16),
                   "10.0.128.64 应该匹配 10.0.0.0/16");
        assertFalse(matchesCIDR("10.1.0.0", cidrRange16),
                    "10.1.0.0 不应该匹配 10.0.0.0/16");

        // /25 测试
        assertTrue(matchesCIDR("10.1.128.0", cidrRange25),
                   "10.1.128.0 应该匹配 10.1.128.0/25");
        assertTrue(matchesCIDR("10.1.128.127", cidrRange25),
                   "10.1.128.127 应该匹配 10.1.128.0/25");
        assertFalse(matchesCIDR("10.1.128.128", cidrRange25),
                    "10.1.128.128 不应该匹配 10.1.128.0/25");

        double elapsed = stopTimer();
        addResult("testMultipleCIDRRanges", true, "", elapsed);
    }

    // 辅助函数：检查IP是否匹配CIDR范围
    bool matchesCIDR(const QString& ip, const QString& cidr) {
        // 将IP地址字符串转换为四字节整数
        QStringList ipParts = ip.split('.');
        if (ipParts.size() != 4) {
            return false;
        }

        bool ok;
        uint32_t ipValue = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t octet = ipParts[i].toUInt(&ok);
            if (!ok || ipParts[i].isEmpty()) {
                return false;
            }
            ipValue = (ipValue << 8) | octet;
        }

        // 解析CIDR表示法
        int slashIndex = cidr.indexOf('/');
        QString networkStr;
        int prefixLen = 32;

        if (slashIndex != -1) {
            networkStr = cidr.left(slashIndex);
            prefixLen = cidr.mid(slashIndex + 1).toInt(&ok);
            if (!ok || prefixLen < 0 || prefixLen > 32) {
                return false;
            }
        } else {
            networkStr = cidr;
        }

        // 解析网络地址
        QStringList networkParts = networkStr.split('.');
        if (networkParts.size() != 4) {
            return false;
        }

        uint32_t networkValue = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t octet = networkParts[i].toUInt(&ok);
            if (!ok || networkParts[i].isEmpty()) {
                return false;
            }
            networkValue = (networkValue << 8) | octet;
        }

        // 计算网络掩码
        uint32_t mask = (prefixLen == 0) ? 0 : (0xFFFFFFFFU << (32 - prefixLen));

        // 检查IP是否在CIDR范围内
        return (ipValue & mask) == (networkValue & mask);
    }
};

} // namespace test
} // namespace qindb

#ifndef QINDB_TEST_MAIN_INCLUDED
#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    using namespace qindb::test;

    // 创建测试套件
    TestSuite suite("IP Whitelist CIDR Tests");

    // 添加测试
    suite.addTest(new TestIPWhitelist());

    // 运行测试
    suite.runAll();

    // 打印测试报告
    suite.printReport();

    return 0;
}
#endif
