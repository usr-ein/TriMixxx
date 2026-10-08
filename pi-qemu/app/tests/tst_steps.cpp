// pipeline/steps: which deploy steps there are, in which order, by which names.

#include "pipeline/steps.h"
#include "util/fail.h"

#include <QTest>

class TestSteps : public QObject {
    Q_OBJECT

private slots:
    void listedInNumberOrder() {
        const auto all = steps::all(PI_QEMU_TEST_DATA "/deploy");
        QCOMPARE(all.size(), 3);
        QCOMPARE(all[0].title(), QString("000_first"));
        QVERIFY(all[0].onDeck);
        QCOMPARE(all[1].title(), QString("001_build-it"));
        QVERIFY(!all[1].onDeck);
        QCOMPARE(all[2].title(), QString("010_last"));
        QCOMPARE(all[2].number, 10);
    }
    void dockerOnlyFromTheHeader() {
        const auto all = steps::all(PI_QEMU_TEST_DATA "/deploy");
        QVERIFY(!all[0].needsDocker);
        QVERIFY(all[1].needsDocker);
        QVERIFY(!all[2].needsDocker); // after the header: not a declaration
    }
    void selectedByNameNumberOrTitle() {
        const auto all = steps::all(PI_QEMU_TEST_DATA "/deploy");
        QCOMPARE(steps::select(all, {}).size(), 3);
        QCOMPARE(steps::select(all, {"all"}).size(), 3);
        const auto some = steps::select(all, {"last", "000"});
        QCOMPARE(some.size(), 2);
        QCOMPARE(some[0].name, QString("first")); // number order, not the order named
        QCOMPARE(some[1].name, QString("last"));
        QCOMPARE(steps::select(all, {"001_build-it"})[0].name, QString("build-it"));
    }
    void unknownStepIsAUsageError() {
        const auto all = steps::all(PI_QEMU_TEST_DATA "/deploy");
        try {
            steps::select(all, {"first", "nosuch"});
            QFAIL("no failure");
        } catch (const Failure& f) {
            QCOMPARE(f.status, 2);
            QVERIFY(f.message.contains("000_first 001_build-it 010_last"));
        }
    }
    void strayFilesAndDuplicatesFail() {
        QVERIFY_THROWS_EXCEPTION(Failure, steps::all(PI_QEMU_TEST_DATA "/deploy-dup"));
        QVERIFY_THROWS_EXCEPTION(Failure, steps::all(PI_QEMU_TEST_DATA "/deploy-bad"));
        QVERIFY_THROWS_EXCEPTION(Failure, steps::all(PI_QEMU_TEST_DATA "/nosuchdir"));
    }
    void theRepoStepsAreWellFormed() {
        const auto all = steps::all(PI_QEMU_SOURCE_DIR "/deploy");
        QVERIFY(all.size() >= 8);
        QCOMPARE(all[0].title(), QString("000_base"));
        QVERIFY(all[0].onDeck);
        for (const auto& s : all)
            if (s.name == "ttymidi" || s.name == "launcher" || s.name == "mixxx") QVERIFY2(s.needsDocker, qPrintable(s.title()));
    }
};

QTEST_GUILESS_MAIN(TestSteps)
#include "tst_steps.moc"
