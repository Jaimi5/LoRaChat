#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cmdFields.h"

namespace {

using Bytes = std::vector<uint8_t>;

Schema schemaOf(const std::string& text) {
    Schema schema;
    EXPECT_TRUE(parseSchema(text, schema)) << text;
    return schema;
}

Bytes encoded(const Schema& schema, const Fields& values) {
    Bytes bytes;
    EXPECT_TRUE(encodeFields(schema, values, bytes));
    return bytes;
}

Fields decoded(const Schema& schema, const Bytes& bytes) {
    Fields fields;
    EXPECT_TRUE(decodeFields(schema, bytes.data(), bytes.size(), fields));
    return fields;
}

bool decodes(const Schema& schema, const Bytes& bytes) {
    Fields fields;
    return decodeFields(schema, bytes.data(), bytes.size(), fields);
}

std::string rendered(const std::string& text, const Schema& reply, const Fields& values,
                     const Schema& args = Schema(), const Fields& argValues = Fields()) {
    std::string out;
    EXPECT_TRUE(renderTemplate(text, reply, values, args, argValues, out)) << text;
    return out;
}

Bytes varint(uint64_t value) {
    Bytes bytes;
    appendVarint(bytes, value);
    return bytes;
}

/** The example schema of the /sleep reply. The command itself declares its own later. */
const char* const SLEEP_REPLY =
    "mode:e(none,light,deep) source:e(default,stored) fraction:u? lightCount:u lightS:u "
    "deepCount:u deepS:u keyWakes:u?";
const char* const SLEEP_TEMPLATE =
    "sleep {0} ({1})[, fraction {2} % (stored)], light {3} x {4} s, deep {5} x {6} s since "
    "power-on[, {7} light sleeps ended by the power key]";

}  // namespace

// --- Varint ---

TEST(CmdVarint, GoldenBytes) {
    EXPECT_EQ(varint(0), (Bytes{0x00}));
    EXPECT_EQ(varint(1), (Bytes{0x01}));
    EXPECT_EQ(varint(127), (Bytes{0x7F}));
    EXPECT_EQ(varint(128), (Bytes{0x80, 0x01}));
    EXPECT_EQ(varint(600), (Bytes{0xD8, 0x04}));
    EXPECT_EQ(varint(16383), (Bytes{0xFF, 0x7F}));
    EXPECT_EQ(varint(16384), (Bytes{0x80, 0x80, 0x01}));
    EXPECT_EQ(varint(0xFFFFFFFFULL), (Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0x0F}));
    EXPECT_EQ(varint(0x100000000ULL), (Bytes{0x80, 0x80, 0x80, 0x80, 0x10}));
}

TEST(CmdVarint, ReadsWhatItWrote) {
    for (uint64_t value : {0ULL, 1ULL, 127ULL, 128ULL, 600ULL, 101325ULL, 0xFFFFFFFFULL}) {
        Bytes bytes = varint(value);
        bytes.push_back(0xEE);
        size_t pos = 0;
        uint64_t out = 99;
        ASSERT_TRUE(readVarint(bytes.data(), bytes.size(), pos, 0xFFFFFFFFULL, out)) << value;
        EXPECT_EQ(out, value);
        EXPECT_EQ(pos, bytes.size() - 1);
    }
}

TEST(CmdVarint, RefusesBadBytes) {
    size_t pos = 0;
    uint64_t out = 99;
    const Bytes cut = {0x80};
    EXPECT_FALSE(readVarint(cut.data(), cut.size(), pos, 0xFFFFFFFFULL, out));
    // 600 in three bytes: a longer form of the same number.
    const Bytes longer = {0xD8, 0x84, 0x00};
    EXPECT_FALSE(readVarint(longer.data(), longer.size(), pos, 0xFFFFFFFFULL, out));
    const Bytes six = {0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    EXPECT_FALSE(readVarint(six.data(), six.size(), pos, 0xFFFFFFFFFFULL, out));
    const Bytes above = {0x80, 0x80, 0x80, 0x80, 0x10};
    EXPECT_FALSE(readVarint(above.data(), above.size(), pos, 0xFFFFFFFFULL, out));
    EXPECT_FALSE(readVarint(nullptr, 0, pos, 0xFFFFFFFFULL, out));
    EXPECT_EQ(pos, 0U);
    EXPECT_EQ(out, 99U);
}

// --- Schema ---

TEST(CmdSchema, ReadsEveryType) {
    Schema schema = schemaOf(
        "n:u t:i/100 mode:e(none,light,deep) addr:h2 ver:v rh:u?/100 age:u? state:e(a,b)? text:s");
    ASSERT_EQ(schema.fields.size(), 9U);
    EXPECT_EQ(schema.fields[0].name, "n");
    EXPECT_EQ(schema.fields[0].type, FieldType::NUMBER_U);
    EXPECT_FALSE(schema.fields[0].optional);
    EXPECT_EQ(schema.fields[0].decimals, 0);
    EXPECT_EQ(schema.fields[1].type, FieldType::NUMBER_I);
    EXPECT_EQ(schema.fields[1].decimals, 2);
    EXPECT_EQ(schema.fields[2].type, FieldType::ENUM_NAME);
    EXPECT_EQ(schema.fields[2].names, (std::vector<std::string>{"none", "light", "deep"}));
    EXPECT_EQ(schema.fields[3].type, FieldType::HEX_BYTES);
    EXPECT_EQ(schema.fields[3].size, 2);
    EXPECT_EQ(schema.fields[4].type, FieldType::VERSION_BYTES);
    EXPECT_TRUE(schema.fields[5].optional);
    EXPECT_EQ(schema.fields[5].decimals, 2);
    EXPECT_TRUE(schema.fields[6].optional);
    EXPECT_EQ(schema.fields[7].type, FieldType::ENUM_NAME);
    EXPECT_TRUE(schema.fields[7].optional);
    EXPECT_EQ(schema.fields[8].type, FieldType::TEXT_REST);
    EXPECT_FALSE(schema.hasGroup());
}

TEST(CmdSchema, TheEmptyTextHasNoFields) {
    Schema schema = schemaOf("");
    EXPECT_TRUE(schema.empty());
}

TEST(CmdSchema, ReadsAGroupAndBytes) {
    Schema routes = schemaOf("count:u routes:*(addr:h2 via:h2 metric:u role:e(a,b)?)");
    ASSERT_EQ(routes.fields.size(), 2U);
    EXPECT_TRUE(routes.hasGroup());
    ASSERT_EQ(routes.members.size(), 4U);
    EXPECT_EQ(routes.members[0].name, "addr");
    EXPECT_EQ(routes.members[2].type, FieldType::NUMBER_U);
    EXPECT_TRUE(routes.members[3].optional);
    Schema sealed = schemaOf("sealed:r");
    EXPECT_EQ(sealed.fields[0].type, FieldType::BYTES_REST);
    EXPECT_TRUE(schemaOf("text:s?").fields[0].optional);
    EXPECT_EQ(schemaOf("big:u/1000000000").fields[0].decimals, 9);
    EXPECT_EQ(schemaOf("key:h32").fields[0].size, 32);
}

TEST(CmdSchema, RefusesBadText) {
    Schema schema = schemaOf("keep:u");
    for (const char* bad : {
             " ",
             "n",
             "n:",
             ":u",
             "n:x",
             "n:u ",
             " n:u",
             "n:u  m:u",
             "n:u,m:u",
             "n:u n:i",
             "bad name:u",
             "n:e",
             "n:e()",
             "n:e(a,)",
             "n:e(a,a)",
             "n:e(a b)",
             "n:e(a",
             "n:h",
             "n:h0",
             "n:h01",
             "n:h33",
             "n:v?",
             "n:h2?",
             "n:u/",
             "n:u/1",
             "n:u/50",
             "n:u/101",
             "n:u/10000000000",
             "n:e(a,b)/10",
             "n:v/10",
             "n:u/10?",
             "n:u??",
             // Text, bytes and a group take the rest, so nothing may follow them.
             "t:s n:u",
             "r:r n:u",
             "g:*(a:u) n:u",
             "g:*()",
             "g:*(a:u",
             "g:*(a:s)",
             "g:*(a:r)",
             "g:*(a:*(b:u))",
             "g:*(a:u)?",
         }) {
        EXPECT_FALSE(parseSchema(bad, schema)) << bad;
    }
    ASSERT_EQ(schema.fields.size(), 1U);
    EXPECT_EQ(schema.fields[0].name, "keep");
}

// --- Bytes ---

TEST(CmdFields, NumbersTravelAsVarints) {
    Schema schema = schemaOf("a:u b:i c:i d:u");
    Fields values;
    values.setU(0, 600).setI(1, -1).setI(2, 2150).setU(3, 0xFFFFFFFFU);
    // Zigzag: -1 is 1, 2150 is 4300.
    EXPECT_EQ(encoded(schema, values),
              (Bytes{0xD8, 0x04, 0x01, 0xCC, 0x21, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F}));
    Fields back = decoded(schema, encoded(schema, values));
    EXPECT_EQ(back.u(0), 600U);
    EXPECT_EQ(back.i(1), -1);
    EXPECT_EQ(back.i(2), 2150);
    EXPECT_EQ(back.u(3), 0xFFFFFFFFU);
}

TEST(CmdFields, TheLimitsOfASignedNumber) {
    Schema schema = schemaOf("a:i b:i c:i");
    Fields values;
    values.setI(0, INT32_MIN).setI(1, INT32_MAX).setI(2, 0);
    Bytes bytes = encoded(schema, values);
    EXPECT_EQ(bytes, (Bytes{0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0xFE, 0xFF, 0xFF, 0xFF, 0x0F, 0x00}));
    Fields back = decoded(schema, bytes);
    EXPECT_EQ(back.i(0), INT32_MIN);
    EXPECT_EQ(back.i(1), INT32_MAX);
    EXPECT_EQ(back.i(2), 0);
}

TEST(CmdFields, EnumHexVersionTextAndBytes) {
    Schema schema = schemaOf("mode:e(none,light,deep) addr:h2 ver:v text:s");
    Fields values;
    const uint8_t addr[] = {0x1A, 0x2B};
    values.setU(0, 2)
        .setBytes(1, addr, sizeof(addr))
        .setVersion(2, 0, 1, 2)
        .setText(3, "hi \xC3\xA0");
    Bytes bytes = encoded(schema, values);
    EXPECT_EQ(bytes, (Bytes{0x02, 0x1A, 0x2B, 0x00, 0x01, 0x02, 'h', 'i', ' ', 0xC3, 0xA0}));
    Fields back = decoded(schema, bytes);
    EXPECT_EQ(back.u(0), 2U);
    EXPECT_EQ(back.bytes(1), (Bytes{0x1A, 0x2B}));
    EXPECT_EQ(back.bytes(2), (Bytes{0, 1, 2}));
    EXPECT_EQ(back.text(3), "hi \xC3\xA0");

    Schema sealed = schemaOf("n:u sealed:r");
    Fields raw;
    const uint8_t data[] = {0x00, 0xFF, 0x00};
    raw.setU(0, 5).setBytes(1, data, sizeof(data));
    EXPECT_EQ(encoded(sealed, raw), (Bytes{0x05, 0x00, 0xFF, 0x00}));
    EXPECT_EQ(decoded(sealed, {0x05, 0x00, 0xFF, 0x00}).bytes(1), (Bytes{0x00, 0xFF, 0x00}));
}

TEST(CmdFields, AnOptionalFieldTravelsAsValuePlusOne) {
    Schema schema = schemaOf("a:u? b:i? c:e(x,y)? d:u");
    Fields values;
    values.setU(0, 0).setI(1, -1).setU(2, 1).setU(3, 7);
    // 0 is 1, zigzag of -1 is 1 and travels as 2, the second name is 2.
    EXPECT_EQ(encoded(schema, values), (Bytes{0x01, 0x02, 0x02, 0x07}));
    Fields back = decoded(schema, {0x01, 0x02, 0x02, 0x07});
    EXPECT_TRUE(back.has(0));
    EXPECT_EQ(back.u(0), 0U);
    EXPECT_EQ(back.i(1), -1);
    EXPECT_EQ(back.u(2), 1U);

    Fields absent;
    absent.setU(3, 7);
    EXPECT_EQ(encoded(schema, absent), (Bytes{0x00, 0x00, 0x00, 0x07}));
    back = decoded(schema, {0x00, 0x00, 0x00, 0x07});
    EXPECT_FALSE(back.has(0));
    EXPECT_FALSE(back.has(1));
    EXPECT_FALSE(back.has(2));
    EXPECT_EQ(back.u(3), 7U);

    // The largest value still fits with the one added.
    Schema one = schemaOf("a:u?");
    Fields top;
    top.setU(0, 0xFFFFFFFFU);
    EXPECT_EQ(encoded(one, top), (Bytes{0x80, 0x80, 0x80, 0x80, 0x10}));
    EXPECT_EQ(decoded(one, {0x80, 0x80, 0x80, 0x80, 0x10}).u(0), 0xFFFFFFFFU);
}

TEST(CmdFields, AbsentOptionalFieldsAtTheEndAreLeftOut) {
    Schema schema = schemaOf("a:u b:u? c:u? d:e(x,y)?");
    Fields values;
    values.setU(0, 5);
    EXPECT_EQ(encoded(schema, values), (Bytes{0x05}));
    values.setU(2, 9);
    // The absent field before a value keeps its zero byte.
    EXPECT_EQ(encoded(schema, values), (Bytes{0x05, 0x00, 0x0A}));
    Fields back = decoded(schema, {0x05, 0x00, 0x0A});
    EXPECT_FALSE(back.has(1));
    EXPECT_EQ(back.u(2), 9U);
    EXPECT_FALSE(back.has(3));
    back = decoded(schema, {0x05});
    EXPECT_FALSE(back.has(1));
    EXPECT_FALSE(back.has(2));

    Schema onlyOptional = schemaOf("a:u? t:s?");
    EXPECT_TRUE(encoded(onlyOptional, Fields()).empty());
    EXPECT_FALSE(decoded(onlyOptional, {}).has(0));
    Fields withText;
    withText.setText(1, "x");
    EXPECT_EQ(encoded(onlyOptional, withText), (Bytes{0x00, 'x'}));
}

TEST(CmdFields, BytesAfterTheLastFieldAreIgnored) {
    Schema schema = schemaOf("a:u b:h1");
    Fields back = decoded(schema, {0x05, 0x76, 0xAA, 0xBB});
    EXPECT_EQ(back.u(0), 5U);
    EXPECT_EQ(back.bytes(1), (Bytes{0x76}));
    EXPECT_TRUE(decodes(schemaOf(""), {0x01, 0x02}));
}

TEST(CmdFields, AGroupRepeatsToTheEnd) {
    Schema schema = schemaOf("count:u routes:*(addr:h2 via:h2 metric:u role:e(gw,node)?)");
    Fields values;
    values.setU(0, 2);
    const uint8_t a[] = {0x1A, 0x2B};
    const uint8_t b[] = {0x3C, 0x4D};
    Fields first;
    first.setBytes(0, a, 2).setBytes(1, a, 2).setU(2, 1).setU(3, 0);
    Fields second;
    second.setBytes(0, b, 2).setBytes(1, a, 2).setU(2, 300);
    values.addRow(first).addRow(second);
    Bytes bytes = encoded(schema, values);
    // Inside a group an absent optional field keeps its zero byte.
    EXPECT_EQ(bytes, (Bytes{0x02, 0x1A, 0x2B, 0x1A, 0x2B, 0x01, 0x01, 0x3C, 0x4D, 0x1A, 0x2B, 0xAC,
                            0x02, 0x00}));
    Fields back = decoded(schema, bytes);
    ASSERT_EQ(back.rowCount(), 2U);
    EXPECT_EQ(back.row(0).bytes(0), (Bytes{0x1A, 0x2B}));
    EXPECT_EQ(back.row(0).u(3), 0U);
    EXPECT_TRUE(back.row(0).has(3));
    EXPECT_EQ(back.row(1).u(2), 300U);
    EXPECT_FALSE(back.row(1).has(3));
    EXPECT_EQ(back.row(2).values().size(), 0U);

    EXPECT_EQ(decoded(schema, {0x00}).rowCount(), 0U);
    // A row that is cut is refused.
    bytes.pop_back();
    EXPECT_FALSE(decodes(schema, bytes));
}

TEST(CmdFields, EncodingRefusesValuesThatDoNotFit) {
    Bytes out = {0xEE};
    Fields empty;
    EXPECT_FALSE(encodeFields(schemaOf("a:u"), empty, out));
    Fields negative;
    negative.setI(0, -1);
    EXPECT_FALSE(encodeFields(schemaOf("a:u"), negative, out));
    Fields big;
    big.setU(0, 0x80000000U);
    EXPECT_FALSE(encodeFields(schemaOf("a:i"), big, out));
    Fields third;
    third.setU(0, 2);
    EXPECT_FALSE(encodeFields(schemaOf("a:e(x,y)"), third, out));
    Fields shortHex;
    const uint8_t one[] = {1};
    shortHex.setBytes(0, one, 1);
    EXPECT_FALSE(encodeFields(schemaOf("a:h2"), shortHex, out));
    EXPECT_FALSE(encodeFields(schemaOf("a:v"), shortHex, out));
    EXPECT_FALSE(encodeFields(schemaOf("a:u"), shortHex, out));
    Fields noText;
    noText.setText(0, "");
    EXPECT_FALSE(encodeFields(schemaOf("a:s"), noText, out));
    Fields nul;
    nul.setText(0, std::string("a\0b", 3));
    EXPECT_FALSE(encodeFields(schemaOf("a:s"), nul, out));
    EXPECT_EQ(out, (Bytes{0xEE}));
    // What was there stays in front.
    Fields fine;
    fine.setU(0, 1);
    EXPECT_TRUE(encodeFields(schemaOf("a:u"), fine, out));
    EXPECT_EQ(out, (Bytes{0xEE, 0x01}));
}

TEST(CmdFields, DecodingRefusesBadBytes) {
    EXPECT_FALSE(decodes(schemaOf("a:u"), {}));
    EXPECT_FALSE(decodes(schemaOf("a:u b:u"), {0x01}));
    EXPECT_FALSE(decodes(schemaOf("a:u"), {0x80}));
    EXPECT_FALSE(decodes(schemaOf("a:u"), {0x80, 0x80, 0x80, 0x80, 0x10}));
    EXPECT_FALSE(decodes(schemaOf("a:e(x,y)"), {0x02}));
    EXPECT_FALSE(decodes(schemaOf("a:e(x,y)?"), {0x03}));
    EXPECT_FALSE(decodes(schemaOf("a:h2"), {0x01}));
    EXPECT_FALSE(decodes(schemaOf("a:v"), {0x01, 0x02}));
    EXPECT_FALSE(decodes(schemaOf("a:s"), {}));
    EXPECT_FALSE(decodes(schemaOf("a:s"), {'a', 0x00, 'b'}));
    Fields keep;
    keep.setU(0, 42);
    EXPECT_FALSE(decodeFields(schemaOf("a:u"), nullptr, 0, keep));
    EXPECT_FALSE(decodeFields(schemaOf("a:u"), nullptr, 3, keep));
    EXPECT_EQ(keep.u(0), 42U);
}

// --- Text of one value ---

TEST(CmdFieldText, TheScalePutsTheDecimalPoint) {
    Schema schema = schemaOf("t:i/100 p:u/100 x:u/10 y:i/1000 n:u");
    struct Case {
        size_t field;
        int64_t number;
        const char* text;
    };
    for (const Case& c : std::vector<Case>{{0, 2150, "21.50"},
                                           {0, -5, "-0.05"},
                                           {0, -1234, "-12.34"},
                                           {0, 0, "0.00"},
                                           {0, 7, "0.07"},
                                           {1, 101325, "1013.25"},
                                           {2, 5, "0.5"},
                                           {3, -1, "-0.001"},
                                           {4, 600, "600"}}) {
        FieldValue value;
        value.present = true;
        value.number = c.number;
        std::string text;
        ASSERT_TRUE(fieldToText(schema.fields[c.field], value, text)) << c.text;
        EXPECT_EQ(text, c.text);
        FieldValue back;
        ASSERT_TRUE(fieldFromText(schema.fields[c.field], c.text, back)) << c.text;
        EXPECT_EQ(back.number, c.number);
    }
}

TEST(CmdFieldText, ReadsWhatAPersonTypes) {
    Schema schema = schemaOf("n:u t:i/100 mode:e(none,light,deep) addr:h2 ver:v sealed:r");
    FieldValue value;
    ASSERT_TRUE(fieldFromText(schema.fields[0], "0600", value));
    EXPECT_EQ(value.number, 600);
    ASSERT_TRUE(fieldFromText(schema.fields[1], "21.5", value));
    EXPECT_EQ(value.number, 2150);
    ASSERT_TRUE(fieldFromText(schema.fields[1], "-21", value));
    EXPECT_EQ(value.number, -2100);
    ASSERT_TRUE(fieldFromText(schema.fields[2], "LIGHT", value));
    EXPECT_EQ(value.number, 1);
    ASSERT_TRUE(fieldFromText(schema.fields[3], "1A2b", value));
    EXPECT_EQ(value.bytes, (Bytes{0x1A, 0x2B}));
    ASSERT_TRUE(fieldFromText(schema.fields[4], "0.1.02", value));
    EXPECT_EQ(value.bytes, (Bytes{0, 1, 2}));
    ASSERT_TRUE(fieldFromText(schema.fields[5], "00FF", value));
    EXPECT_EQ(value.bytes, (Bytes{0x00, 0xFF}));
}

TEST(CmdFieldText, RefusesWhatIsNotAValue) {
    Schema schema = schemaOf("n:u t:i/100 mode:e(none,light,deep) addr:h2 ver:v sealed:r");
    FieldValue value;
    value.number = 42;
    for (const char* bad : {"", "-1", "+1", "1.0", "1 ", " 1", "x", "4294967296", "1e3"}) {
        EXPECT_FALSE(fieldFromText(schema.fields[0], bad, value)) << bad;
    }
    for (const char* bad : {"", "-", "1.", ".5", "1.234", "--1", "1.2.3", "21474836.48"}) {
        EXPECT_FALSE(fieldFromText(schema.fields[1], bad, value)) << bad;
    }
    for (const char* bad : {"", "ligh", "lights", "1"}) {
        EXPECT_FALSE(fieldFromText(schema.fields[2], bad, value)) << bad;
    }
    for (const char* bad : {"", "1a", "1a2b3c", "1a2g", "0x1a"}) {
        EXPECT_FALSE(fieldFromText(schema.fields[3], bad, value)) << bad;
    }
    for (const char* bad : {"", "1.2", "1.2.3.4", "1.2.256", "1.2.x", "1..2", "1.2.3+g1"}) {
        EXPECT_FALSE(fieldFromText(schema.fields[4], bad, value)) << bad;
    }
    for (const char* bad : {"", "abc", "zz"}) {
        EXPECT_FALSE(fieldFromText(schema.fields[5], bad, value)) << bad;
    }
    EXPECT_EQ(value.number, 42);
    // The limits of a signed number with a scale.
    ASSERT_TRUE(fieldFromText(schema.fields[1], "-21474836.48", value));
    EXPECT_EQ(value.number, INT32_MIN);
    ASSERT_TRUE(fieldFromText(schema.fields[1], "21474836.47", value));
    EXPECT_EQ(value.number, INT32_MAX);
}

TEST(CmdFieldText, ReadsTheStartOfAVersion) {
    uint8_t parts[FIELD_VERSION_BYTES] = {9, 9, 9};
    ASSERT_TRUE(parseVersionPrefix("0.1.2+g2bfdeeb.dirty", parts));
    EXPECT_EQ(parts[0], 0);
    EXPECT_EQ(parts[1], 1);
    EXPECT_EQ(parts[2], 2);
    ASSERT_TRUE(parseVersionPrefix("10.20.255", parts));
    EXPECT_EQ(parts[2], 255);
    for (const char* bad : {"", "1.2", "1.2.", "v1.2.3", "1.2.256", "1.2.1000", "test build"}) {
        EXPECT_FALSE(parseVersionPrefix(bad, parts)) << bad;
    }
    EXPECT_EQ(parts[0], 10);
}

TEST(CmdFieldText, ReadsTheBuildPartOfAVersion) {
    uint8_t commit[VERSION_COMMIT_BYTES] = {9, 9, 9, 9};
    bool dirty = true;
    ASSERT_TRUE(parseVersionBuild("0.1.2+g2bfdeeb1", commit, dirty));
    EXPECT_EQ(std::vector<uint8_t>(commit, commit + VERSION_COMMIT_BYTES),
              (std::vector<uint8_t>{0x2B, 0xFD, 0xEE, 0xB1}));
    EXPECT_FALSE(dirty);
    ASSERT_TRUE(parseVersionBuild("10.20.255+gABCDEF01.dirty", commit, dirty));
    EXPECT_EQ(std::vector<uint8_t>(commit, commit + VERSION_COMMIT_BYTES),
              (std::vector<uint8_t>{0xAB, 0xCD, 0xEF, 0x01}));
    EXPECT_TRUE(dirty);
    // No commit, a commit of another length, or text behind it that is not ".dirty".
    for (const char* bad : {"", "0.1.2", "0.1.2+g", "0.1.2+g2bfdeeb", "0.1.2+g2bfdeeb.dirty",
                            "0.1.2+g2bfdeeb12", "0.1.2+g2bfdeebx", "0.1.2+g2bfdeeb1.dirt",
                            "0.1.2+g2bfdeeb1.dirty2", "0.1.2+2bfdeeb1", "test build"}) {
        EXPECT_FALSE(parseVersionBuild(bad, commit, dirty)) << bad;
    }
    EXPECT_EQ(commit[0], 0xAB);
    EXPECT_TRUE(dirty);
}

// --- Command lines ---

TEST(CmdLine, ReadsArgumentsByPosition) {
    Schema schema = schemaOf("seconds:u mode:e(pull,ap)?");
    Fields fields;
    ASSERT_TRUE(parseArgs(schema, "3600 ap", fields));
    EXPECT_EQ(fields.u(0), 3600U);
    EXPECT_EQ(fields.u(1), 1U);
    ASSERT_TRUE(parseArgs(schema, "  3600  ", fields));
    EXPECT_EQ(fields.u(0), 3600U);
    EXPECT_FALSE(fields.has(1));
    fields.setU(0, 42);
    for (const char* bad : {"", "ap", "3600 ap more", "3600 other", "x"}) {
        EXPECT_FALSE(parseArgs(schema, bad, fields)) << bad;
    }
    EXPECT_EQ(fields.u(0), 42U);
    EXPECT_TRUE(parseArgs(schemaOf(""), "", fields));
    EXPECT_TRUE(parseArgs(schemaOf(""), "  ", fields));
    EXPECT_FALSE(parseArgs(schemaOf(""), "more", fields));
    EXPECT_FALSE(parseArgs(schemaOf("g:*(a:u)"), "1", fields));
}

TEST(CmdLine, TextTakesTheRestOfTheLine) {
    Schema schema = schemaOf("size:u text:s");
    Fields fields;
    ASSERT_TRUE(parseArgs(schema, "2 hello there  world", fields));
    EXPECT_EQ(fields.u(0), 2U);
    EXPECT_EQ(fields.text(1), "hello there  world");
    EXPECT_FALSE(parseArgs(schema, "2", fields));
    ASSERT_TRUE(parseArgs(schemaOf("size:u text:s?"), "2", fields));
    EXPECT_FALSE(fields.has(1));
}

TEST(CmdLine, BuildsTheCanonicalLine) {
    Schema schema = schemaOf("seconds:u mode:e(pull,ap)?");
    Fields fields;
    fields.setU(0, 3600).setU(1, 1);
    std::string line;
    ASSERT_TRUE(canonicalLine("/maint.open", schema, fields, line));
    EXPECT_EQ(line, "/maint.open 3600 ap");
    Fields one;
    one.setU(0, 3600);
    ASSERT_TRUE(canonicalLine("/maint.open", schema, one, line));
    EXPECT_EQ(line, "/maint.open 3600");
    ASSERT_TRUE(canonicalLine("/sleep", schemaOf(""), Fields(), line));
    EXPECT_EQ(line, "/sleep");

    line = "keep";
    EXPECT_FALSE(canonicalLine("/maint.open", schema, Fields(), line));
    // An argument can be absent only if every argument after it is absent.
    Schema gaps = schemaOf("a:u? b:u?");
    Fields gap;
    gap.setU(1, 5);
    EXPECT_FALSE(canonicalLine("/x", gaps, gap, line));
    EXPECT_FALSE(canonicalLine("/x", schemaOf("g:*(a:u)"), Fields(), line));
    EXPECT_EQ(line, "keep");
}

TEST(CmdLine, EncodeThenRenderGivesTheCanonicalLineBack) {
    struct Case {
        const char* name;
        const char* schema;
        const char* line;
    };
    for (const Case& c : std::vector<Case>{
             {"/report.set", "seconds:u", "/report.set 600"},
             {"/report.set", "seconds:u", "/report.set 0"},
             {"/sleep.set", "mode:e(none,light,deep)", "/sleep.set light"},
             {"/sleep", "", "/sleep"},
             {"/maint.open", "seconds:u mode:e(pull,ap)?", "/maint.open 3600"},
             {"/maint.open", "seconds:u mode:e(pull,ap)?", "/maint.open 3600 ap"},
             {"/displayText", "text:s", "/displayText Hola, m\xC3\xB3n  2"},
             {"/maint.wifi", "sealed:r", "/maint.wifi 00ff10"},
             {"/x", "t:i/100 addr:h2 ver:v", "/x -0.05 1a2b 0.1.2"},
         }) {
        EXPECT_TRUE(isCanonicalLine(c.line, c.name, schemaOf(c.schema))) << c.line;
    }
}

TEST(CmdLine, ALineThatIsNotCanonicalIsDetected) {
    struct Case {
        const char* name;
        const char* schema;
        const char* line;
    };
    for (const Case& c : std::vector<Case>{
             {"/report.set", "seconds:u", "/report.set 0600"},
             {"/report.set", "seconds:u", "/report.set  600"},
             {"/report.set", "seconds:u", "/report.set 600 "},
             {"/report.set", "seconds:u", "/REPORT.SET 600"},
             {"/report.set", "seconds:u", "/report.set"},
             {"/report.set", "seconds:u", "/report.set default"},
             {"/report.set", "seconds:u", "/report.setx 600"},
             {"/sleep.set", "mode:e(none,light,deep)", "/sleep.set LIGHT"},
             {"/sleep", "", "/sleep "},
             {"/sleep", "", "/sleep now"},
             {"/x", "t:i/100", "/x 21.5"},
             {"/x", "t:i/100", "/x -0.00"},
             {"/x", "addr:h2", "/x 1A2B"},
             {"/x", "ver:v", "/x 0.1.02"},
             {"/displayText", "text:s", "/displayText  two spaces first"},
             {"/displayText", "text:s", "/displayText ends with a space "},
         }) {
        EXPECT_FALSE(isCanonicalLine(c.line, c.name, schemaOf(c.schema))) << c.line;
    }
}

TEST(CmdLine, UsageComesFromTheSchema) {
    EXPECT_EQ(usageFromSchema(schemaOf("mode:e(none,light,deep)")), "<none, light or deep>");
    EXPECT_EQ(usageFromSchema(schemaOf("role:e(gateway,sensor)")), "<gateway or sensor>");
    EXPECT_EQ(usageFromSchema(schemaOf("seconds:u mode:e(ap)?")), "<seconds> [ap]");
    EXPECT_EQ(usageFromSchema(schemaOf("seconds:u? text:s")), "[seconds] <text>");
    EXPECT_EQ(usageFromSchema(schemaOf("")), "");
}

// --- Templates ---

TEST(CmdTemplate, FillsPlaceholders) {
    Schema reply = schemaOf("chip:e(BMP280,BME280) addr:h1 t:i/100 ver:v text:s");
    Fields values;
    const uint8_t addr[] = {0x76};
    values.setU(0, 1).setBytes(1, addr, 1).setI(2, -5).setVersion(3, 0, 1, 2).setText(4, "ok");
    EXPECT_EQ(rendered("{0} at 0x{1}: {2} C, v{3} ({4})", reply, values),
              "BME280 at 0x76: -0.05 C, v0.1.2 (ok)");
    EXPECT_EQ(rendered("plain text", reply, values), "plain text");
    EXPECT_EQ(rendered("", reply, values), "");
    EXPECT_EQ(rendered("{2}{2}", reply, values), "-0.05-0.05");
}

TEST(CmdTemplate, QuotesTheRequestArguments) {
    Schema args = schemaOf("mode:e(none,light,deep) seconds:u?");
    Fields in;
    in.setU(0, 1);
    EXPECT_EQ(rendered("Sleep mode set to {a0}, restarting", schemaOf(""), Fields(), args, in),
              "Sleep mode set to light, restarting");
    EXPECT_EQ(rendered("{a0}[ for {a1} s]", schemaOf(""), Fields(), args, in), "light");
    in.setU(1, 30);
    EXPECT_EQ(rendered("{a0}[ for {a1} s]", schemaOf(""), Fields(), args, in), "light for 30 s");
}

TEST(CmdTemplate, ABracketedPartVanishesWhenAValueInItIsAbsent) {
    Schema reply = schemaOf("t:i/100 p:u/100 rh:u?/100 age:u?");
    const std::string text = "{0} C[ {2} %RH] {1} hPa[, read {3} s ago]";
    Fields values;
    values.setI(0, 2150).setU(1, 101325);
    EXPECT_EQ(rendered(text, reply, values), "21.50 C 1013.25 hPa");
    values.setU(2, 4550);
    EXPECT_EQ(rendered(text, reply, values), "21.50 C 45.50 %RH 1013.25 hPa");
    values.setU(3, 0);
    EXPECT_EQ(rendered(text, reply, values), "21.50 C 45.50 %RH 1013.25 hPa, read 0 s ago");
    // One absent value removes the whole part.
    Fields partly;
    partly.setI(0, 1).setU(1, 2).setU(3, 9);
    EXPECT_EQ(rendered("[{2} and {3}]end", reply, partly), "end");
    // Nested brackets: an absent value removes only the innermost part around it.
    EXPECT_EQ(rendered("[age {3}[, rh {2}]]", reply, partly), "age 9");
    EXPECT_EQ(rendered("[rh {2}[, age {3}]]", reply, partly), "");
    // Outside brackets an absent value writes nothing.
    EXPECT_EQ(rendered("rh={2}.", reply, partly), "rh=.");
    // A part without placeholders always stays.
    EXPECT_EQ(rendered("[always]", reply, partly), "always");
}

TEST(CmdTemplate, WritesTheRowsOfAGroup) {
    Schema reply = schemaOf("routes:*(addr:h2 via:h2 metric:u role:e(gw,node)?)");
    const uint8_t a[] = {0x1A, 0x2B};
    const uint8_t b[] = {0x3C, 0x4D};
    Fields values;
    Fields first;
    first.setBytes(0, a, 2).setBytes(1, a, 2).setU(2, 1).setU(3, 0);
    Fields second;
    second.setBytes(0, b, 2).setBytes(1, a, 2).setU(2, 3);
    values.addRow(first).addRow(second);
    EXPECT_EQ(rendered("Routes: {0*{0} via {1} ({2})[ {3}]*, }.", reply, values),
              "Routes: 1a2b via 1a2b (1) gw, 3c4d via 1a2b (3).");
    EXPECT_EQ(rendered("{0*{0} *}", reply, values), "1a2b 3c4d ");
    EXPECT_EQ(rendered("{0*{0}}", reply, values), "1a2b3c4d");
    // A group without rows is absent.
    EXPECT_EQ(rendered("Routes:[ {0*{0}*, }][ none{0*{0}}]", reply, Fields()), "Routes:");
    EXPECT_EQ(rendered("{0*{0}*, }", reply, Fields()), "");
}

TEST(CmdTemplate, ABackslashTakesTheNextCharacter) {
    Schema reply = schemaOf("n:u");
    Fields values;
    values.setU(0, 5);
    EXPECT_EQ(rendered("\\[{0}\\] \\{x\\} a\\\\b", reply, values), "[5] {x} a\\b");
}

TEST(CmdTemplate, ChecksATemplateAgainstTheSchemas) {
    Schema reply = schemaOf("n:u routes:*(addr:h2 metric:u)");
    Schema args = schemaOf("mode:e(a,b)");
    for (const char* good : {"", "text", "{0}", "{a0}", "[{0}]", "{1*{0} {1}*, }", "{1*{0}}",
                             "{1*[{1}] {a0}*\\}}", "a * b"}) {
        EXPECT_TRUE(checkTemplate(good, reply, args)) << good;
    }
    for (const char* bad : {
             "{",
             "}",
             "[",
             "]",
             "{0",
             "{}",
             "{x}",
             "{2}",
             "{a1}",
             "{a}",
             "[{0}",
             "{0}]",
             "{0000}",
             // A group needs its row text, and only a group has one.
             "{1}",
             "{0*x}",
             "{a0*x}",
             "{1*{2}}",
             "{1*{0}",
             "{1*{0}*, ",
             "{1*{1*{0}}}",
             "\\",
             "{1*{0}*a\\",
         }) {
        EXPECT_FALSE(checkTemplate(bad, reply, args)) << bad;
    }
    std::string out = "keep";
    EXPECT_FALSE(renderTemplate("{2}", reply, Fields(), args, Fields(), out));
    EXPECT_EQ(out, "keep");
}

TEST(CmdTemplate, RefusesAValueThatDoesNotFitItsField) {
    Schema reply = schemaOf("mode:e(a,b) addr:h2");
    Fields values;
    values.setU(0, 5);
    std::string out;
    EXPECT_FALSE(renderTemplate("{0}", reply, values, Schema(), Fields(), out));
    const uint8_t one[] = {1};
    values.setU(0, 0).setBytes(1, one, 1);
    EXPECT_FALSE(renderTemplate("{1}", reply, values, Schema(), Fields(), out));
    EXPECT_TRUE(renderTemplate("{0}", reply, values, Schema(), Fields(), out));
}

// --- The examples of the design, at the level of the fields ---

TEST(CmdExamples, SleepSetLight) {
    Schema args = schemaOf("mode:e(none,light,deep)");
    Fields in;
    ASSERT_TRUE(parseArgs(args, "light", in));
    EXPECT_EQ(encoded(args, in), (Bytes{0x01}));
    std::string line;
    ASSERT_TRUE(canonicalLine("/sleep.set", args, decoded(args, {0x01}), line));
    EXPECT_EQ(line, "/sleep.set light");
    // The reply has no fields: its text quotes the argument of the request.
    EXPECT_TRUE(encoded(schemaOf(""), Fields()).empty());
    EXPECT_EQ(rendered("Sleep mode set to {a0}, restarting", schemaOf(""), Fields(), args, in),
              "Sleep mode set to light, restarting");
}

TEST(CmdExamples, ReportSet600) {
    Schema args = schemaOf("seconds:u");
    Fields in;
    ASSERT_TRUE(parseArgs(args, "600", in));
    EXPECT_EQ(encoded(args, in), (Bytes{0xD8, 0x04}));
    std::string line;
    ASSERT_TRUE(canonicalLine("/report.set", args, decoded(args, {0xD8, 0x04}), line));
    EXPECT_EQ(line, "/report.set 600");
    EXPECT_FALSE(isCanonicalLine("/report.set 0600", "/report.set", args));
}

TEST(CmdExamples, SleepReplyFitsInElevenBytesOfFields) {
    Schema reply = schemaOf(SLEEP_REPLY);
    Fields values;
    values.setU(0, 1).setU(1, 1).setU(2, 40).setU(3, 1200).setU(4, 54000).setU(5, 0).setU(6, 0);
    values.setU(7, 3);
    const Bytes golden = {0x01, 0x01, 0x29, 0xB0, 0x09, 0xF0, 0xA5, 0x03, 0x00, 0x00, 0x04};
    EXPECT_EQ(encoded(reply, values), golden);
    EXPECT_EQ(rendered(SLEEP_TEMPLATE, reply, decoded(reply, golden)),
              "sleep light (stored), fraction 40 % (stored), light 1200 x 54000 s, deep 0 x 0 s "
              "since power-on, 3 light sleeps ended by the power key");
    // A board without the power key and with the default fraction.
    Fields plain;
    plain.setU(0, 0).setU(1, 0).setU(3, 0).setU(4, 0).setU(5, 0).setU(6, 0);
    EXPECT_EQ(encoded(reply, plain), (Bytes{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(rendered(SLEEP_TEMPLATE, reply, plain),
              "sleep none (default), light 0 x 0 s, deep 0 x 0 s since power-on");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
