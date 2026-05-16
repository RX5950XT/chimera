#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include "QmpInput.h"

using namespace chimera::input;

class TestQmpInput : public QObject {
    Q_OBJECT

private slots:
    void buildsAbsoluteMouseMoveEvents() {
        const QJsonObject cmd = QmpInput::buildMouseMoveCommand(1919, 1079, 1920, 1080);
        QCOMPARE(cmd.value("execute").toString(), QStringLiteral("input-send-event"));

        const QJsonArray events = cmd.value("arguments").toObject().value("events").toArray();
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.at(0).toObject().value("type").toString(), QStringLiteral("abs"));
        QCOMPARE(events.at(0).toObject().value("data").toObject().value("axis").toString(), QStringLiteral("x"));
        QCOMPARE(events.at(0).toObject().value("data").toObject().value("value").toInt(), 0x7fff);
        QCOMPARE(events.at(1).toObject().value("data").toObject().value("axis").toString(), QStringLiteral("y"));
        QCOMPARE(events.at(1).toObject().value("data").toObject().value("value").toInt(), 0x7fff);
    }

    void buildsMouseButtonAtPosition() {
        const QJsonObject cmd = QmpInput::buildMouseButtonCommand(1, true, 0, 0, 1920, 1080);
        const QJsonArray events = cmd.value("arguments").toObject().value("events").toArray();

        QCOMPARE(events.size(), 3);
        QCOMPARE(events.at(2).toObject().value("type").toString(), QStringLiteral("btn"));
        const QJsonObject data = events.at(2).toObject().value("data").toObject();
        QCOMPARE(data.value("button").toString(), QStringLiteral("right"));
        QCOMPARE(data.value("down").toBool(), true);
    }
};

QTEST_MAIN(TestQmpInput)
#include "test_qmp_input.moc"
