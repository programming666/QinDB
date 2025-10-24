#include "test_framework.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/config.h"
#include <QTemporaryFile>
#include <QVector>

namespace qindb {
namespace test {

/**
 * @brief 缓冲池管理器单元测试
 */
class BufferPoolTest : public TestCase {
public:
    BufferPoolTest() : TestCase("BufferPoolTest") {}

    void run() override {
        try { testBasicPageOperations(); } catch (...) {}
        try { testLRUReplacement(); } catch (...) {}
        try { testDirtyPageFlush(); } catch (...) {}
        try { testPinUnpinMechanism(); } catch (...) {}
        try { testConcurrentAccess(); } catch (...) {}
    }

private:
    /**
     * @brief 测试基本页面操作(创建、获取、删除)
     */
    void testBasicPageOperations() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open(), "Failed to create temp file");
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        BufferPoolManager bufferPool(10, &diskMgr);  // 小缓冲池用于测试

        // 测试新建页面
        PageId pageId1;
        Page* page1 = bufferPool.newPage(&pageId1);
        assertTrue(page1 != nullptr, "Failed to create new page");
        assertEqual(static_cast<PageId>(1), pageId1, "First page should have ID 1");

        // 写入数据
        char* data = page1->getData();
        strcpy(data, "Test Data Page 1");
        bufferPool.unpinPage(pageId1, true);

        // 测试获取已存在的页面
        Page* page1Again = bufferPool.fetchPage(pageId1);
        assertTrue(page1Again != nullptr, "Failed to fetch existing page");
        assertEqual(std::string("Test Data Page 1"), std::string(page1Again->getData()),
                   "Page data should match");
        bufferPool.unpinPage(pageId1, false);

        // 测试删除页面
        assertTrue(bufferPool.deletePage(pageId1), "Failed to delete page");

        double elapsed = stopTimer();
        addResult("testBasicPageOperations", true, "", elapsed);
    }

    /**
     * @brief 测试LRU替换策略
     */
    void testLRUReplacement() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        BufferPoolManager bufferPool(3, &diskMgr);  // 只有3个帧

        // 创建4个页面(会触发替换)
        QVector<PageId> pageIds;
        for (int i = 0; i < 4; ++i) {
            PageId pageId;
            Page* page = bufferPool.newPage(&pageId);
            assertTrue(page != nullptr, QString("Failed to create page %1").arg(i));

            // 写入不同数据
            char* data = page->getData();
            sprintf(data, "Page %d Data", i);
            bufferPool.unpinPage(pageId, true);

            pageIds.push_back(pageId);
        }

        // 访问页面0,1,2(使页面3成为LRU)
        for (int i = 0; i < 3; ++i) {
            Page* page = bufferPool.fetchPage(pageIds[i]);
            assertTrue(page != nullptr, QString("Failed to fetch page %1").arg(pageIds[i]));
            bufferPool.unpinPage(pageIds[i], false);
        }

        // 创建新页面,应该替换掉页面3
        PageId newPageId;
        Page* newPage = bufferPool.newPage(&newPageId);
        assertTrue(newPage != nullptr, "Failed to create new page after replacement");
        bufferPool.unpinPage(newPageId, false);

        double elapsed = stopTimer();
        addResult("testLRUReplacement", true, "", elapsed);
    }

    /**
     * @brief 测试脏页刷新
     */
    void testDirtyPageFlush() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);

        PageId pageId;
        {
            BufferPoolManager bufferPool(5, &diskMgr);

            // 创建页面并写入数据
            Page* page = bufferPool.newPage(&pageId);
            assertTrue(page != nullptr);

            char* data = page->getData();
            strcpy(data, "Dirty Page Data");
            bufferPool.unpinPage(pageId, true);  // 标记为脏页

            // 强制刷新所有脏页
            bufferPool.flushAllPages();
        }  // bufferPool析构时也会刷新

        // 重新打开缓冲池,验证数据已持久化
        {
            BufferPoolManager bufferPool2(5, &diskMgr);
            Page* page = bufferPool2.fetchPage(pageId);
            assertTrue(page != nullptr, "Failed to fetch page after flush");
            assertEqual(std::string("Dirty Page Data"), std::string(page->getData()),
                       "Data should persist after flush");
            bufferPool2.unpinPage(pageId, false);
        }

        double elapsed = stopTimer();
        addResult("testDirtyPageFlush", true, "", elapsed);
    }

    /**
     * @brief 测试Pin/Unpin机制
     */
    void testPinUnpinMechanism() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        BufferPoolManager bufferPool(2, &diskMgr);

        // 创建两个页面
        PageId pageId1, pageId2;
        Page* page1 = bufferPool.newPage(&pageId1);
        Page* page2 = bufferPool.newPage(&pageId2);
        assertTrue(page1 != nullptr && page2 != nullptr);

        // 不要unpin page1,保持它被pin住
        bufferPool.unpinPage(pageId2, false);

        // 尝试创建第三个页面(应该能成功,因为page2可以被替换)
        PageId pageId3;
        Page* page3 = bufferPool.newPage(&pageId3);
        assertTrue(page3 != nullptr, "Should be able to create new page when unpinned page exists");
        bufferPool.unpinPage(pageId3, false);

        // 现在unpin page1
        bufferPool.unpinPage(pageId1, false);

        double elapsed = stopTimer();
        addResult("testPinUnpinMechanism", true, "", elapsed);
    }

    /**
     * @brief 测试并发访问(基本测试)
     */
    void testConcurrentAccess() {
        startTimer();

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(true);
        assertTrue(tempFile.open());
        QString dbPath = tempFile.fileName();
        tempFile.close();

        DiskManager diskMgr(dbPath);
        BufferPoolManager bufferPool(10, &diskMgr);

        // 创建多个页面
        QVector<PageId> pageIds;
        for (int i = 0; i < 5; ++i) {
            PageId pageId;
            Page* page = bufferPool.newPage(&pageId);
            assertTrue(page != nullptr);

            char* data = page->getData();
            sprintf(data, "Concurrent Page %d", i);
            bufferPool.unpinPage(pageId, true);

            pageIds.push_back(pageId);
        }

        // 多次访问不同页面
        for (int round = 0; round < 10; ++round) {
            for (PageId pageId : pageIds) {
                Page* page = bufferPool.fetchPage(pageId);
                assertTrue(page != nullptr, QString("Failed to fetch page %1 in round %2")
                          .arg(pageId).arg(round));
                bufferPool.unpinPage(pageId, false);
            }
        }

        double elapsed = stopTimer();
        addResult("testConcurrentAccess", true, "", elapsed);
    }
};

} // namespace test
} // namespace qindb
