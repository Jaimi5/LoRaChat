#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "cmdCatalogue.h"
#include "cmdTyped.h"
#include "firmwareCatalogue.h"
// The fixture reads limits from NetCore and OtaCore. These includes let PlatformIO find both.
#include "installMode.h"
#include "maintWindowPlan.h"

/**
 * Counts the heap bytes that the command declarations take: every block that a string or a
 * vector of them owns. The count is made twice, with the object sizes of the host and with
 * those of an ESP32, where a pointer has four bytes and a string object 24.
 */
namespace {

/** Object sizes of one platform. */
struct Sizes {
    const char* name;
    size_t stringObject;
    /** Longest text a string object holds without a heap block. */
    size_t stringInline;
    size_t fieldSpec;
    size_t errorDetail;
    size_t commandCode;
    size_t commandSpec;
    /** Bytes the allocator adds to a block, and the size it rounds blocks up to. */
    size_t blockOverhead;
    size_t blockAlign;
};

const Sizes HOST = {"host",
                    sizeof(std::string),
                    15,
                    sizeof(FieldSpec),
                    sizeof(ErrorDetail),
                    sizeof(CommandCode),
                    sizeof(CommandSpec),
                    8,
                    16};
// libstdc++ on a 32-bit target: a string of 24 bytes, a vector of 12, and the multi heap of
// ESP-IDF with 4 bytes of header per block.
const Sizes ESP32 = {"ESP32", 24, 15, 40, 16, 52, 144, 4, 4};

size_t block(const Sizes& sizes, size_t bytes) {
    if (bytes == 0) return 0;
    const size_t rounded = (bytes + sizes.blockAlign - 1) / sizes.blockAlign * sizes.blockAlign;
    return rounded + sizes.blockOverhead;
}

size_t held(const Sizes& sizes, const std::string& text) {
    return text.size() > sizes.stringInline ? block(sizes, text.size() + 1) : 0;
}

size_t held(const Sizes& sizes, const std::vector<std::string>& texts) {
    size_t total = block(sizes, texts.size() * sizes.stringObject);
    for (const std::string& text : texts) total += held(sizes, text);
    return total;
}

size_t held(const Sizes& sizes, const std::vector<FieldSpec>& fields) {
    size_t total = block(sizes, fields.size() * sizes.fieldSpec);
    for (const FieldSpec& field : fields)
        total += held(sizes, field.name) + held(sizes, field.names);
    return total;
}

size_t held(const Sizes& sizes, const TypedDeclaration& declaration) {
    size_t total = held(sizes, declaration.argsText) + held(sizes, declaration.replyText) +
                   held(sizes, declaration.replyTemplate) + held(sizes, declaration.args.fields) +
                   held(sizes, declaration.args.members) + held(sizes, declaration.reply.fields) +
                   held(sizes, declaration.reply.members) +
                   block(sizes, declaration.details.size() * sizes.errorDetail);
    for (const ErrorDetail& detail : declaration.details) total += held(sizes, detail.names);
    return total;
}

size_t heldCodes(const Sizes& sizes, const std::vector<CommandCode>& codes) {
    size_t total = block(sizes, codes.size() * sizes.commandCode);
    for (const CommandCode& code : codes)
        total += held(sizes, code.text) + held(sizes, code.detail);
    return total;
}

size_t held(const Sizes& sizes, const CommandSpec& spec) {
    return held(sizes, spec.name) + held(sizes, spec.usage) + heldCodes(sizes, spec.codes) +
           held(sizes, spec.args) + held(sizes, spec.reply) + held(sizes, spec.replyTemplate);
}

/** The heap bytes of the command declarations of the firmware. */
struct Count {
    /** The catalogue of every command, which was kept for /cmds and for the error texts. */
    size_t catalogue = 0;
    /** The parsed declaration and the three texts that every typed command kept. */
    size_t declarations = 0;
    /** The error texts and the help text that every command kept as its own copies. */
    size_t commandTexts = 0;
    /** The largest declaration, which a command now reads while it runs. */
    size_t largest = 0;
    std::string largestName;
    size_t typedCommands = 0;
};

Count count(const Sizes& sizes) {
    const CommandCatalogue catalogue = firmwareCatalogue();
    Count out;
    out.catalogue = block(sizes, catalogue.specs().size() * sizes.commandSpec);
    for (const CommandSpec& spec : catalogue.specs()) {
        out.catalogue += held(sizes, spec);
        out.commandTexts += heldCodes(sizes, spec.codes);
        if (spec.id == 0) continue;
        out.typedCommands++;
        TypedDeclaration declaration;
        EXPECT_TRUE(typedDeclarationOf(spec, declaration)) << spec.name;
        // The command kept the three texts next to the declaration, which holds them again.
        const size_t bytes = held(sizes, declaration) + held(sizes, spec.args) +
                             held(sizes, spec.reply) + held(sizes, spec.replyTemplate);
        out.declarations += bytes;
        if (held(sizes, declaration) > out.largest) {
            out.largest = held(sizes, declaration);
            out.largestName = spec.name;
        }
    }
    return out;
}

}  // namespace

TEST(CommandHeap, CountsWhatTheDeclarationsHeldAndHoldNow) {
    for (const Sizes& sizes : {HOST, ESP32}) {
        const Count c = count(sizes);
        const size_t before = c.catalogue + c.declarations + c.commandTexts;
        std::cout << "--- " << sizes.name << "\n"
                  << "catalogue kept for /cmds and error texts: " << c.catalogue << " B\n"
                  << "parsed declarations and their texts, " << c.typedCommands
                  << " commands: " << c.declarations << " B\n"
                  << "error texts copied into the commands: " << c.commandTexts << " B\n"
                  << "held before: " << before << " B\n"
                  << "held now: 0 B of these, the commands point at string literals\n"
                  << "while a command runs: up to " << c.largest << " B (" << c.largestName << ")\n"
                  << "while /cmds answers: " << c.catalogue << " B and the pages\n";
        EXPECT_EQ(c.typedCommands, 28U);
        // A command that runs reads its own declaration and frees it again.
        EXPECT_LT(c.largest, 4000U) << sizes.name;
        // /cmds builds the catalogue for the time of its reply.
        EXPECT_LT(c.catalogue, 24000U) << sizes.name;
    }
}

TEST(CommandHeap, TheHostCountUsesTheRealObjectSizes) {
    // The sizes of the ESP32 column are written by hand. These hold on the host.
    EXPECT_EQ(HOST.stringObject, 32U);
    EXPECT_GE(HOST.commandSpec, ESP32.commandSpec);
    EXPECT_GE(HOST.fieldSpec, ESP32.fieldSpec);
    std::string shortText(HOST.stringInline, 'x');
    EXPECT_EQ(shortText.capacity(), HOST.stringInline);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
