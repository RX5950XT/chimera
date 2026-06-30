#include "FileUtils.h"
#include <QtTest/QtTest>
#include <filesystem>

class TestFileUtils : public QObject {
    Q_OBJECT

private slots:
    void ensureDirSucceedsWhenDirectoryAlreadyExists() {
        const auto dir = std::filesystem::temp_directory_path() / "chimera_fileutils_existing_dir_test";
        std::filesystem::remove_all(dir);
        QVERIFY(std::filesystem::create_directories(dir));
        QVERIFY(chimera::utils::FileUtils::ensureDir(dir));
        std::filesystem::remove_all(dir);
    }
};

QTEST_MAIN(TestFileUtils)
#include "test_file_utils.moc"
