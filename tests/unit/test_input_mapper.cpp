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

    void testInsertMapping() {
        auto &mapper = InputMapper::instance();
        mapper.clearMappings();

        InputMapping a; a.type = "tap"; a.key = "A";
        InputMapping b; b.type = "tap"; b.key = "B";
        InputMapping c; c.type = "tap"; c.key = "C";
        mapper.addMapping(a);
        mapper.addMapping(c);
        mapper.insertMapping(1, b);  // Insert B between A and C

        QCOMPARE(mapper.getMappings().size(), size_t(3));
        QCOMPARE(mapper.getMappings()[0].key, std::string("A"));
        QCOMPARE(mapper.getMappings()[1].key, std::string("B"));
        QCOMPARE(mapper.getMappings()[2].key, std::string("C"));
    }

    void testInsertMappingAtEnd() {
        auto &mapper = InputMapper::instance();
        mapper.clearMappings();
        InputMapping m; m.type = "tap"; m.key = "X";
        mapper.insertMapping(999, m);  // index beyond end — appends
        QCOMPARE(mapper.getMappings().size(), size_t(1));
        QCOMPARE(mapper.getMappings()[0].key, std::string("X"));
    }

    void testRejectsUnsafeSchemeName() {
        auto &mapper = InputMapper::instance();
        QVERIFY(!mapper.loadScheme("../outside"));
        QVERIFY(!mapper.saveScheme("../outside"));
    }
};

QTEST_MAIN(TestInputMapper)
#include "test_input_mapper.moc"
