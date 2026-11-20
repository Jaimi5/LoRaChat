#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file
 * @brief Typed fields of a binary command: the arguments of a request and the values of a reply.
 *
 * A command declares its fields once, as a schema text. The bytes on LoRa carry only the values,
 * by position. Text for people is made from the same schema.
 *
 * Schema text: fields separated by one space, each "name:type", then "?" and "/scale" where
 * they apply. The empty text is a schema without fields.
 *
 * | Type     | Bytes                                         | Text                          |
 * |----------|-----------------------------------------------|-------------------------------|
 * | u        | varint: 7 bits per byte, low group first      | decimal                       |
 * | i        | zigzag, then varint                           | decimal with "-"              |
 * | e(a,b,c) | one byte, the position of the name from 0     | the name                      |
 * | h<n>     | n raw bytes, n from 1 to 32                   | lowercase hex                 |
 * | v        | three bytes                                   | "major.minor.patch"           |
 * | s        | UTF-8 text up to the end                      | the text                      |
 * | r        | bytes up to the end                           | lowercase hex                 |
 * | *(...)   | the fields in the brackets, repeated to the end | by the template             |
 *
 * "?" makes a u, i or e field optional: 0 means absent and a value travels as value + 1 (for i
 * the zigzag value + 1). Absent optional fields at the end of the bytes are left out. On an s or
 * r field "?" allows the empty value and costs no byte.
 * "/10", "/100" up to "/1000000000" on a u or i field puts the decimal point in the text: the
 * bytes carry 2150 and the text says "21.50".
 *
 * Choices where the design left the detail open:
 * - u and i hold 32 bits. A varint has at most five bytes and its shortest form is the only one
 *   accepted.
 * - An e field has at most 254 names. A byte that names no position is refused.
 * - s, r and a group take the rest of the bytes, so each is allowed as the last field only and
 *   a schema has one group at most. A group holds u, i, e, h and v fields. Inside a group an
 *   absent optional field always takes its zero byte.
 * - An empty s or r field counts as absent, so it needs "?" to be allowed.
 * - Bytes after the last field are ignored, so a newer sender may append fields.
 * - In a command line the arguments come by position, separated by spaces. An s field takes the
 *   rest of the line. An argument can be absent only if every argument after it is absent. A
 *   group cannot be written as an argument.
 * - Names in a schema hold letters, digits and '_'. The names of an e field hold any character
 *   except space, ',', '(', ')', '|', ';' and control characters.
 */

/** The types u, i, e, h, v, s, r and the group, in that order. */
enum class FieldType : uint8_t {
    NUMBER_U,
    NUMBER_I,
    ENUM_NAME,
    HEX_BYTES,
    VERSION_BYTES,
    TEXT_REST,
    BYTES_REST,
    GROUP_ROWS,
};

/** Most bytes of an h field. */
constexpr size_t FIELD_HEX_MAX_BYTES = 32;
/** Bytes of a v field. */
constexpr size_t FIELD_VERSION_BYTES = 3;
/** Most names of an e field. */
constexpr size_t FIELD_ENUM_MAX_NAMES = 254;
/** Most digits after the decimal point that a scale gives. */
constexpr uint8_t FIELD_MAX_DECIMALS = 9;

/** One declared field. */
struct FieldSpec {
    std::string name;
    FieldType type = FieldType::NUMBER_U;
    bool optional = false;
    /** Digits after the decimal point in the text. "/100" gives 2. */
    uint8_t decimals = 0;
    /** Bytes of an h field. */
    uint8_t size = 0;
    /** Names of an e field. */
    std::vector<std::string> names;
};

/** The declared fields of a request or of a reply. */
struct Schema {
    std::vector<FieldSpec> fields;
    /** Fields of one row of the group, when the last field is a group. */
    std::vector<FieldSpec> members;

    bool empty() const { return fields.empty(); }
    bool hasGroup() const { return !fields.empty() && fields.back().type == FieldType::GROUP_ROWS; }
};

/** @return false, leaving @p out unchanged, if @p text is not a schema. */
bool parseSchema(const std::string& text, Schema& out);

/** One value. A number for u, i and e fields, bytes for h, v, s and r fields. */
struct FieldValue {
    bool present = false;
    int64_t number = 0;
    std::vector<uint8_t> bytes;
};

/**
 * @brief The values of the fields of one schema, by position.
 *
 * A handler reads its arguments from one and writes its reply into another. A field that was
 * never set is absent.
 */
class Fields {
public:
    /** @return true if field @p index has a value. An empty text or byte field has none. */
    bool has(size_t index) const;
    /** @return the value of a u or e field, 0 when absent. */
    uint32_t u(size_t index) const;
    /** @return the value of an i field, 0 when absent. */
    int32_t i(size_t index) const;
    /** @return the bytes of an h, v, s or r field, empty when absent. */
    const std::vector<uint8_t>& bytes(size_t index) const;
    /** @return the bytes of an s field as text. */
    std::string text(size_t index) const;

    /** Sets a u field, or an e field to the position of its name. */
    Fields& setU(size_t index, uint32_t value);
    Fields& setI(size_t index, int32_t value);
    Fields& setBytes(size_t index, const uint8_t* data, size_t size);
    Fields& setText(size_t index, const std::string& value);
    /** Sets a v field. */
    Fields& setVersion(size_t index, uint8_t major, uint8_t minor, uint8_t patch);
    /** Sets field @p index to @p value as it is. */
    Fields& setValue(size_t index, const FieldValue& value);
    /** Adds one row to the group. The fields of @p row are the members of the group. */
    Fields& addRow(const Fields& row);

    size_t rowCount() const { return rows_.size(); }
    /** @return row @p index of the group, empty if there is none. */
    Fields row(size_t index) const;
    const std::vector<std::vector<FieldValue>>& rows() const { return rows_; }

    /** @return the value of field @p index, or nullptr if it was never set. */
    const FieldValue* value(size_t index) const;
    const std::vector<FieldValue>& values() const { return values_; }

private:
    FieldValue& slot(size_t index);

    std::vector<FieldValue> values_;
    std::vector<std::vector<FieldValue>> rows_;
};

/** Appends @p value to @p out as a varint. */
void appendVarint(std::vector<uint8_t>& out, uint64_t value);

/**
 * @brief Reads a varint of at most five bytes at @p pos and moves @p pos behind it.
 * @return false, leaving @p pos and @p out unchanged, if the bytes end early, the value is
 *         above @p max or the form is not the shortest one.
 */
bool readVarint(const uint8_t* data, size_t size, size_t& pos, uint64_t max, uint64_t& out);

/**
 * @brief Appends the bytes of @p values to @p out.
 * @return false, leaving @p out unchanged, if a required field is absent or a value does not
 *         fit its field.
 */
bool encodeFields(const Schema& schema, const Fields& values, std::vector<uint8_t>& out);

/**
 * @brief Reads the bytes of the fields of @p schema.
 * @return false, leaving @p out unchanged, if a required field is missing or a value is not
 *         valid for its field.
 */
bool decodeFields(const Schema& schema, const uint8_t* data, size_t size, Fields& out);

/** @return false if @p value is absent or does not fit @p spec. @p out gets its exact text. */
bool fieldToText(const FieldSpec& spec, const FieldValue& value, std::string& out);

/**
 * @brief Reads one value as a person may type it: leading zeros, fewer decimals, any case in
 *        hex digits and in the names of an e field.
 * @return false, leaving @p out unchanged, if @p text is not a value of @p spec.
 */
bool fieldFromText(const FieldSpec& spec, const std::string& text, FieldValue& out);

/**
 * @brief Reads the arguments of a command line, without the command name.
 * @return false, leaving @p out unchanged, if a required argument is missing, one is not valid,
 *         or words are left over.
 */
bool parseArgs(const Schema& schema, const std::string& args, Fields& out);

/**
 * @brief Builds the canonical command line: the name, then each argument in its exact text,
 *        separated by one space. The signature of a command covers this line.
 * @return false if the values cannot be written as a line.
 */
bool canonicalLine(const std::string& name, const Schema& schema, const Fields& values,
                   std::string& out);

/**
 * @return true if @p line is the canonical line of command @p name: its arguments, encoded and
 *         written back, give @p line again. "/report.set 0600" is not canonical.
 */
bool isCanonicalLine(const std::string& line, const std::string& name, const Schema& schema);

/** @return the arguments for people: "<name>", "[name]" when optional, "<a, b or c>". */
std::string usageFromSchema(const Schema& schema);

/**
 * @brief Reads the three numbers that start a version text such as "0.1.2+g2bfdeeb".
 * @return false, leaving @p out unchanged, if the text does not start with three numbers from
 *         0 to 255 separated by dots.
 */
bool parseVersionPrefix(const std::string& text, uint8_t out[FIELD_VERSION_BYTES]);

/**
 * @brief Renders a reply as text.
 *
 * Template text:
 * - "{0}" is field 0 of the reply in its exact text, "{a0}" is argument 0 of the request.
 * - "[...]" is left out when a value named inside it is absent. Brackets may be nested, and an
 *   absent value removes only the innermost brackets around it.
 * - "{2*row*separator}" writes the row text once per row of group field 2, with the separator
 *   between rows. Inside the row text "{0}" is member 0 of the row. The separator and its "*"
 *   may be left out.
 * - "\" takes the next character as it is.
 * An absent value outside brackets writes nothing.
 * @return false, leaving @p out unchanged, if the template is not valid for the schemas or a
 *         value does not fit its field.
 */
bool renderTemplate(const std::string& text, const Schema& reply, const Fields& values,
                    const Schema& args, const Fields& argValues, std::string& out);

/** @return true if @p text is a valid template for a reply of @p reply to a request of @p args. */
bool checkTemplate(const std::string& text, const Schema& reply, const Schema& args);
