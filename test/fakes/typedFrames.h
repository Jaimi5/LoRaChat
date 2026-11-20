#pragma once

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cmdAuth.h"
#include "cmdFields.h"
#include "cmdFrame.h"
#include "cmdTyped.h"

/** Counter and tag of the signed frames in the golden vectors. */
constexpr uint32_t GOLDEN_COUNTER = 0x01020304;
const CommandTag GOLDEN_TAG = {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};

/** Bytes of the signature at the end of a signed request: the counter, then the tag. */
inline std::vector<uint8_t> goldenSignature() {
    return {0x04, 0x03, 0x02, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
}

/** @return the request frame of @p line, as a gateway builds it. */
inline std::vector<uint8_t> requestFrame(const TypedDeclaration& declaration,
                                         const std::string& name, const std::string& line,
                                         bool isSigned) {
    EXPECT_TRUE(isCanonicalLine(line, name, declaration.args)) << line;
    Fields in;
    EXPECT_TRUE(parseArgs(declaration.args, line.substr(name.size()), in)) << line;
    BinaryCommandFrame frame;
    frame.id = declaration.id;
    frame.isSigned = isSigned;
    frame.counter = GOLDEN_COUNTER;
    frame.tag = GOLDEN_TAG;
    EXPECT_TRUE(encodeFields(declaration.args, in, frame.payload));
    return frame.encode();
}

/** @return the line a node signs for, built from the bytes of a request frame. */
inline std::string lineOf(const TypedDeclaration& declaration, const std::string& name,
                          const std::vector<uint8_t>& bytes) {
    BinaryCommandFrame frame;
    EXPECT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), frame));
    EXPECT_EQ(frame.id, declaration.id);
    Fields in;
    EXPECT_TRUE(decodeFields(declaration.args, frame.payload.data(), frame.payload.size(), in));
    std::string line;
    EXPECT_TRUE(canonicalLine(name, declaration.args, in, line));
    return line;
}

/** @return the reply frame of a typed outcome, as a node builds it. */
inline std::vector<uint8_t> replyFrame(const TypedDeclaration& declaration,
                                       const TypedResult& result, const Fields& out) {
    BinaryCommandFrame frame;
    frame.response = true;
    frame.status = result.status;
    EXPECT_TRUE(encodeTypedReply(declaration, result, out, frame.payload));
    return frame.encode();
}

/** @return the text a gateway makes of a reply frame for the request with arguments @p in. */
inline CommandResult textOf(const TypedDeclaration& declaration, const Fields& in,
                            const std::vector<uint8_t>& bytes) {
    BinaryCommandFrame frame;
    EXPECT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), frame));
    EXPECT_TRUE(frame.response);
    TypedResult result;
    Fields out;
    EXPECT_TRUE(decodeTypedReply(declaration, frame.status, frame.payload.data(),
                                 frame.payload.size(), result, out));
    return typedReplyText(declaration, in, result, out);
}
