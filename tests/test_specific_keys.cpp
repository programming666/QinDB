
#include <iostream>
#include <QCoreApplication>
#include <QTemporaryFile>
#include "qindb/generic_bplustree.h"
#include "qindb/buffer_pool_manager.h"
#include "qindb/disk_manager.h"
#include "qindb/config.h"

using namespace qindb;

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false);
    tempFile.open();
    QString dbPath = tempFile.fileName();
    tempFile.close();
    
    DiskManager diskMgr(dbPath);
    Config& config = Config::instance();
    BufferPoolManager bufferPool(config.getBufferPoolSize(), &diskMgr);
    GenericBPlusTree tree(&bufferPool, DataType::INT);
    
    // 插入10000个键
    for (int i = 1; i <= 10000; ++i) {
        tree.insert(QVariant(i), i);
    }
    
    // 测试100-102周围的键
    std::cout << "Testing keys around 101:" << std::endl;
    for (int i = 98; i <= 104; ++i) {
        RowId foundRowId = 0;
        bool found = tree.search(QVariant(i), foundRowId);
        std::cout << "  Key " << i << ": " << (found ? "FOUND" : "NOT FOUND") << std::endl;
    }
    
    // 测试5200-5202周围的键
    std::cout << "
Testing keys around 5201:" << std::endl;
    for (int i = 5198; i <= 5204; ++i) {
        RowId foundRowId = 0;
        bool found = tree.search(QVariant(i), foundRowId);
        std::cout << "  Key " << i << ": " << (found ? "FOUND" : "NOT FOUND") << std::endl;
    }
    
    return 0;
}
