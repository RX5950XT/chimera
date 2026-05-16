#include <QTest>
#include "InputMapper.h"

using namespace chimera::input;

class TestInputMapper : public QObject {
    Q_OBJECT

private slots:
    void testAddMapping() {
        auto &mapper = InputMapper::instance();
        mapper.clearMappings();

        InputMapping m;
        m.type = "tap";
        m.x = 50.0f;
        m.y = 50.0f;
        m.key = "Space";
        mapper.addMapping(m);

        QCOMPARE(mapper.getMappings().size(), size_t(1));
        QCOMPARE(mapper.getMappings()[0].key, std::string("Space"));
    }

    void testFindMapping() {
        auto &mapper = InputMapper::instance();
        mapper.clearMappings();

        InputMapping m;
        m.type = "tap";
        m.key = "A";
        mapper.addMapping(m);

        auto *found = mapper.findMappingByKey("A");
        QVERIFY(found != nullptr);
        QCOMPARE(found->key, std::string("A"));
    }

    void testNormToPixel() {
        QCOMPARE(InputMapper::normToPixel(50.0f, 1920), 960);
        QCOMPARE(InputMapper::normToPixel(100.0f, 1080), 1080);
    }

    void testRejectsUnsafeSchemeName() {
        auto &mapper = InputMapper::instance();
        QVERIFY(!mapper.loadScheme("../outside"));
        QVERIFY(!mapper.saveScheme("../outside"));
    }
};

QTEST_MAIN(TestInputMapper)
#include "test_input_mapper.moc"
