#include "UuidTest.h"

#include "domain/model/Uuid.h"

#include <QtTest/QtTest>

#include <unordered_set>

using namespace manga::domain;

void UuidTest::testV4Generation() {
    const Uuid u1 = Uuid::generateV4();
    const Uuid u2 = Uuid::generateV4();

    QVERIFY(!u1.isNil());
    QVERIFY(!u2.isNil());
    QVERIFY(u1 != u2);

    const auto& bytes = u1.bytes();
    // Version 4 check: byte 6 high nibble == 4
    QCOMPARE((bytes[6] >> 4), 0x04);
    // Variant 1 check: byte 8 high 2 bits == 10 (0x80)
    QCOMPARE((bytes[8] >> 6), 0x02);
}

void UuidTest::testParseValid() {
    const std::string validStr = "123e4567-e89b-42d3-a456-426614174000";
    const auto result = Uuid::parse(validStr);
    QVERIFY(result.has_value());
    QCOMPARE(QString::fromStdString(result->toString()), QString::fromStdString(validStr));

    // Case-insensitivity test
    const std::string upperStr = "123E4567-E89B-42D3-A456-426614174000";
    const auto upperRes = Uuid::parse(upperStr);
    QVERIFY(upperRes.has_value());
    QCOMPARE(*result, *upperRes);
}

void UuidTest::testParseInvalid() {
    // Too short
    QVERIFY(!Uuid::parse("123e4567-e89b-42d3-a456-42661417400").has_value());
    // Too long
    QVERIFY(!Uuid::parse("123e4567-e89b-42d3-a456-4266141740000").has_value());
    // Invalid hyphen placement
    QVERIFY(!Uuid::parse("123e4567e-89b-42d3-a456-426614174000").has_value());
    // Non-hex character
    QVERIFY(!Uuid::parse("123e4567-e89b-42d3-a456-42661417400z").has_value());
    // Non-v4 UUID (version 1)
    const auto v1Res = Uuid::parse("123e4567-e89b-12d3-a456-426614174000");
    QVERIFY(!v1Res.has_value());
    QCOMPARE(v1Res.error(), UuidParseError::InvalidVersion);
    // Invalid variant (0 instead of 8, 9, a, b)
    const auto varRes = Uuid::parse("123e4567-e89b-42d3-0456-426614174000");
    QVERIFY(!varRes.has_value());
    QCOMPARE(varRes.error(), UuidParseError::InvalidVariant);
    // Empty
    QVERIFY(!Uuid::parse("").has_value());
}

void UuidTest::testStringRoundTrip() {
    for (int i = 0; i < 100; ++i) {
        const Uuid orig = Uuid::generateV4();
        const std::string str = orig.toString();
        QCOMPARE(str.size(), size_t{36});
        const auto parsed = Uuid::parse(str);
        QVERIFY(parsed.has_value());
        QCOMPARE(*parsed, orig);
    }
}

void UuidTest::testComparisonAndHashing() {
    const Uuid u1 = Uuid::generateV4();
    const Uuid u2 = Uuid::generateV4();
    const Uuid u1Copy = u1;

    QVERIFY(u1 == u1Copy);
    QVERIFY(u1 != u2);

    std::unordered_set<Uuid> set;
    set.insert(u1);
    set.insert(u2);
    QCOMPARE(set.size(), size_t{2});
    QVERIFY(set.contains(u1Copy));
}

void UuidTest::testStronglyTypedIds() {
    const ProjectId pid = ProjectId::generate();
    const PageId paid = PageId::generate();
    const RegionId rid = RegionId::generate();
    const GlossaryId gid = GlossaryId::generate();

    QVERIFY(!pid.value.isNil());
    QVERIFY(!paid.value.isNil());
    QVERIFY(!rid.value.isNil());
    QVERIFY(!gid.value.isNil());

    const auto parsedPid = ProjectId::parse(pid.toString());
    QVERIFY(parsedPid.has_value());
    QCOMPARE(*parsedPid, pid);
}
