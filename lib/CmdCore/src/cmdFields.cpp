#include "cmdFields.h"

#include "cmdText.h"

namespace {

constexpr uint64_t UINT_MAX_VALUE = 0xFFFFFFFFULL;
constexpr int64_t INT_MIN_VALUE = -2147483648LL;
constexpr int64_t INT_MAX_VALUE = 2147483647LL;
constexpr size_t VARINT_MAX_BYTES = 5;
constexpr uint8_t VARINT_MORE = 0x80;
constexpr uint8_t VARINT_BITS = 7;
constexpr uint32_t BYTE_MAX = 255;
constexpr size_t MAX_INDEX_DIGITS = 3;
constexpr uint32_t MAX_INDEX = 999;

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isNameChar(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isEnumNameChar(char c) {
    const unsigned char code = static_cast<unsigned char>(c);
    return code > ' ' && code != 0x7F && c != ',' && c != '(' && c != ')' && c != '|' && c != ';';
}

bool isNumber(FieldType type) {
    return type == FieldType::NUMBER_U || type == FieldType::NUMBER_I ||
           type == FieldType::ENUM_NAME;
}

bool takesTheRest(FieldType type) {
    return type == FieldType::TEXT_REST || type == FieldType::BYTES_REST ||
           type == FieldType::GROUP_ROWS;
}

size_t fixedSize(const FieldSpec& spec) {
    return spec.type == FieldType::VERSION_BYTES ? FIELD_VERSION_BYTES : spec.size;
}

bool hasName(const std::vector<FieldSpec>& fields, const std::string& name) {
    for (const FieldSpec& field : fields) {
        if (field.name == name) return true;
    }
    return false;
}

bool parseFieldList(const std::string& text, size_t& pos, bool inGroup,
                    std::vector<FieldSpec>& fields, std::vector<FieldSpec>& members);

bool parseEnumNames(const std::string& text, size_t& pos, std::vector<std::string>& names) {
    if (pos >= text.size() || text[pos] != '(') return false;
    pos++;
    for (;;) {
        size_t start = pos;
        while (pos < text.size() && isEnumNameChar(text[pos])) pos++;
        if (pos == start || pos >= text.size()) return false;
        std::string name = text.substr(start, pos - start);
        for (const std::string& other : names) {
            if (other == name) return false;
        }
        names.push_back(name);
        if (names.size() > FIELD_ENUM_MAX_NAMES) return false;
        char end = text[pos++];
        if (end == ')') return true;
        if (end != ',') return false;
    }
}

bool parseHexSize(const std::string& text, size_t& pos, uint8_t& size) {
    size_t start = pos;
    while (pos < text.size() && isDigit(text[pos])) pos++;
    uint32_t value = 0;
    if (!parseDecimal(text.substr(start, pos - start), FIELD_HEX_MAX_BYTES, value)) return false;
    if (value == 0 || text[start] == '0') return false;
    size = static_cast<uint8_t>(value);
    return true;
}

/** Reads "/1" followed by zeros. */
bool parseScale(const std::string& text, size_t& pos, uint8_t& decimals) {
    if (pos >= text.size() || text[pos] != '1') return false;
    pos++;
    uint8_t zeros = 0;
    while (pos < text.size() && text[pos] == '0') {
        if (zeros == FIELD_MAX_DECIMALS) return false;
        zeros++;
        pos++;
    }
    if (zeros == 0) return false;
    decimals = zeros;
    return true;
}

bool parseField(const std::string& text, size_t& pos, bool inGroup, FieldSpec& spec,
                std::vector<FieldSpec>& members) {
    size_t start = pos;
    while (pos < text.size() && isNameChar(text[pos])) pos++;
    if (pos == start || pos + 1 >= text.size() || text[pos] != ':') return false;
    spec.name = text.substr(start, pos - start);
    pos++;
    switch (text[pos++]) {
        case 'u':
            spec.type = FieldType::NUMBER_U;
            break;
        case 'i':
            spec.type = FieldType::NUMBER_I;
            break;
        case 'v':
            spec.type = FieldType::VERSION_BYTES;
            break;
        case 's':
            spec.type = FieldType::TEXT_REST;
            break;
        case 'r':
            spec.type = FieldType::BYTES_REST;
            break;
        case 'e':
            spec.type = FieldType::ENUM_NAME;
            if (!parseEnumNames(text, pos, spec.names)) return false;
            break;
        case 'h':
            spec.type = FieldType::HEX_BYTES;
            if (!parseHexSize(text, pos, spec.size)) return false;
            break;
        case '*': {
            spec.type = FieldType::GROUP_ROWS;
            if (inGroup || pos >= text.size() || text[pos] != '(') return false;
            pos++;
            std::vector<FieldSpec> unused;
            if (!parseFieldList(text, pos, true, members, unused)) return false;
            if (pos >= text.size() || text[pos] != ')') return false;
            pos++;
            break;
        }
        default:
            return false;
    }
    if (inGroup && takesTheRest(spec.type)) return false;
    if (pos < text.size() && text[pos] == '?') {
        if (!isNumber(spec.type) && spec.type != FieldType::TEXT_REST &&
            spec.type != FieldType::BYTES_REST) {
            return false;
        }
        spec.optional = true;
        pos++;
    }
    if (pos < text.size() && text[pos] == '/') {
        if (spec.type != FieldType::NUMBER_U && spec.type != FieldType::NUMBER_I) return false;
        pos++;
        if (!parseScale(text, pos, spec.decimals)) return false;
    }
    return true;
}

/** Reads fields up to the end of the text, or inside a group up to its ')'. */
bool parseFieldList(const std::string& text, size_t& pos, bool inGroup,
                    std::vector<FieldSpec>& fields, std::vector<FieldSpec>& members) {
    for (;;) {
        FieldSpec spec;
        if (!parseField(text, pos, inGroup, spec, members)) return false;
        if (hasName(fields, spec.name)) return false;
        fields.push_back(spec);
        if (pos >= text.size()) return !inGroup;
        if (text[pos] == ')') return inGroup;
        if (text[pos] != ' ' || takesTheRest(spec.type)) return false;
        pos++;
    }
}

/** @return false if @p number is not a value of the u, i or e field @p spec. */
bool numberToWire(const FieldSpec& spec, int64_t number, uint64_t& raw) {
    switch (spec.type) {
        case FieldType::NUMBER_U:
            if (number < 0 || static_cast<uint64_t>(number) > UINT_MAX_VALUE) return false;
            raw = static_cast<uint64_t>(number);
            return true;
        case FieldType::NUMBER_I: {
            if (number < INT_MIN_VALUE || number > INT_MAX_VALUE) return false;
            // Zigzag: 0, -1, 1, -2 become 0, 1, 2, 3.
            const uint64_t magnitude = static_cast<uint64_t>(number < 0 ? -(number + 1) : number);
            raw = magnitude * 2 + (number < 0 ? 1 : 0);
            return true;
        }
        case FieldType::ENUM_NAME:
            if (number < 0 || static_cast<uint64_t>(number) >= spec.names.size()) return false;
            raw = static_cast<uint64_t>(number);
            return true;
        default:
            break;
    }
    return false;
}

bool wireToNumber(const FieldSpec& spec, uint64_t raw, int64_t& number) {
    if (spec.type == FieldType::ENUM_NAME && raw >= spec.names.size()) return false;
    if (raw > UINT_MAX_VALUE) return false;
    if (spec.type == FieldType::NUMBER_I) {
        const int64_t half = static_cast<int64_t>(raw / 2);
        number = raw % 2 == 0 ? half : -half - 1;
    } else {
        number = static_cast<int64_t>(raw);
    }
    return true;
}

bool encodeScalar(const FieldSpec& spec, const FieldValue* value, std::vector<uint8_t>& out) {
    const bool present = value != nullptr && value->present;
    if (!present) {
        if (!spec.optional) return false;
        if (isNumber(spec.type)) out.push_back(0);
        return true;
    }
    if (isNumber(spec.type)) {
        uint64_t raw = 0;
        if (!value->bytes.empty() || !numberToWire(spec, value->number, raw)) return false;
        if (spec.optional) raw++;
        if (spec.type == FieldType::ENUM_NAME) {
            out.push_back(static_cast<uint8_t>(raw));
        } else {
            appendVarint(out, raw);
        }
        return true;
    }
    if (spec.type == FieldType::GROUP_ROWS) return false;
    if (!takesTheRest(spec.type) && value->bytes.size() != fixedSize(spec)) return false;
    if (spec.type == FieldType::TEXT_REST) {
        for (uint8_t byte : value->bytes) {
            if (byte == 0) return false;
        }
    }
    out.insert(out.end(), value->bytes.begin(), value->bytes.end());
    return true;
}

/** Reads the field at @p pos, which is inside the data. */
bool decodeScalar(const FieldSpec& spec, const uint8_t* data, size_t size, size_t& pos,
                  FieldValue& value) {
    value = FieldValue();
    if (isNumber(spec.type)) {
        uint64_t raw = 0;
        if (spec.type == FieldType::ENUM_NAME) {
            raw = data[pos++];
        } else if (!readVarint(data, size, pos, UINT_MAX_VALUE + (spec.optional ? 1 : 0), raw)) {
            return false;
        }
        if (spec.optional) {
            if (raw == 0) return true;
            raw--;
        }
        value.present = true;
        return wireToNumber(spec, raw, value.number);
    }
    if (spec.type == FieldType::GROUP_ROWS) return false;
    size_t length = takesTheRest(spec.type) ? size - pos : fixedSize(spec);
    if (length > size - pos) return false;
    value.bytes.assign(data + pos, data + pos + length);
    pos += length;
    if (spec.type == FieldType::TEXT_REST) {
        for (uint8_t byte : value.bytes) {
            if (byte == 0) return false;
        }
    }
    value.present = true;
    return true;
}

/** @return the digits of @p magnitude with the decimal point @p decimals digits from the end. */
std::string decimalText(uint64_t magnitude, uint8_t decimals, bool negative) {
    std::string digits = std::to_string(magnitude);
    if (decimals > 0) {
        if (digits.size() <= decimals) digits.insert(0, decimals + 1 - digits.size(), '0');
        digits.insert(digits.size() - decimals, 1, '.');
    }
    return negative ? "-" + digits : digits;
}

/** Reads "digits" or "digits.digits" with at most @p decimals digits after the point. */
bool parseMagnitude(const std::string& text, uint8_t decimals, uint64_t& out) {
    // Every valid value fits well below this, so the sum cannot overflow 64 bits.
    constexpr uint64_t LIMIT = UINT_MAX_VALUE + 1;
    const size_t point = text.find('.');
    const std::string whole = text.substr(0, point);
    const std::string fraction = point == std::string::npos ? "" : text.substr(point + 1);
    if (whole.empty()) return false;
    if (point != std::string::npos && (fraction.empty() || fraction.size() > decimals)) {
        return false;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < whole.size() + decimals; i++) {
        char c = '0';
        if (i < whole.size()) {
            c = whole[i];
        } else if (i - whole.size() < fraction.size()) {
            c = fraction[i - whole.size()];
        }
        if (!isDigit(c)) return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > LIMIT) return false;
    }
    out = value;
    return true;
}

bool numberFromText(const FieldSpec& spec, const std::string& text, int64_t& out) {
    const bool negative = spec.type == FieldType::NUMBER_I && !text.empty() && text[0] == '-';
    uint64_t magnitude = 0;
    if (!parseMagnitude(negative ? text.substr(1) : text, spec.decimals, magnitude)) return false;
    const int64_t number =
        negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    uint64_t raw = 0;
    if (!numberToWire(spec, number, raw)) return false;
    out = number;
    return true;
}

char lowerCase(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool sameIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (lowerCase(a[i]) != lowerCase(b[i])) return false;
    }
    return true;
}

/** The exact name wins over a name that differs in case only. */
bool enumFromText(const FieldSpec& spec, const std::string& text, int64_t& out) {
    for (size_t i = 0; i < spec.names.size(); i++) {
        if (spec.names[i] == text) {
            out = static_cast<int64_t>(i);
            return true;
        }
    }
    for (size_t i = 0; i < spec.names.size(); i++) {
        if (sameIgnoringCase(spec.names[i], text)) {
            out = static_cast<int64_t>(i);
            return true;
        }
    }
    return false;
}

/** Reads exactly three numbers separated by dots. */
bool versionFromText(const std::string& text, std::vector<uint8_t>& out) {
    size_t dots = 0;
    for (char c : text) {
        if (c == '.') dots++;
        if (c != '.' && !isDigit(c)) return false;
    }
    uint8_t parts[FIELD_VERSION_BYTES] = {};
    if (dots != FIELD_VERSION_BYTES - 1 || !parseVersionPrefix(text, parts)) return false;
    out.assign(parts, parts + FIELD_VERSION_BYTES);
    return true;
}

std::string enumUsage(const std::vector<std::string>& names) {
    std::string text;
    for (size_t i = 0; i < names.size(); i++) {
        if (i > 0) text += i + 1 == names.size() ? " or " : ", ";
        text += names[i];
    }
    return text;
}

/** Renders a template. With empty values it only checks the template against the schemas. */
class TemplateRenderer {
public:
    TemplateRenderer(const std::string& text, const Schema& reply, const Fields& values,
                     const Schema& args, const Fields& argValues)
        : text_(text), reply_(reply), values_(values), args_(args), argValues_(argValues) {}

    bool run(std::string& out) {
        std::string result;
        bool absent = false;
        const Scope top{&reply_.fields, &values_.values(), true};
        if (!sequence(top, "", result, absent) || pos_ != text_.size()) return false;
        out = result;
        return true;
    }

private:
    /** The fields that "{0}" names: the reply, or one row of its group. */
    struct Scope {
        const std::vector<FieldSpec>* specs;
        const std::vector<FieldValue>* values;
        bool top;
    };

    static bool isStop(const char* stops, char c) {
        for (; *stops != '\0'; stops++) {
            if (*stops == c) return true;
        }
        return false;
    }

    /**
     * Renders up to the end of the text or to a character of @p stops, which is not consumed.
     * @p absent is set when a value named at this level is absent.
     */
    bool sequence(const Scope& scope, const char* stops, std::string& out, bool& absent) {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (isStop(stops, c)) return true;
            pos_++;
            if (c == '\\') {
                if (pos_ >= text_.size()) return false;
                out += text_[pos_++];
            } else if (c == '[') {
                std::string inner;
                bool innerAbsent = false;
                if (!sequence(scope, "]", inner, innerAbsent) || pos_ >= text_.size()) {
                    return false;
                }
                pos_++;
                if (!innerAbsent) out += inner;
            } else if (c == '{') {
                if (!placeholder(scope, out, absent)) return false;
            } else if (c == ']' || c == '}') {
                return false;
            } else {
                out += c;
            }
        }
        return true;
    }

    /** Renders the placeholder whose '{' was just read. */
    bool placeholder(const Scope& scope, std::string& out, bool& absent) {
        const bool isArg = pos_ < text_.size() && text_[pos_] == 'a';
        if (isArg) pos_++;
        const size_t start = pos_;
        while (pos_ < text_.size() && isDigit(text_[pos_])) pos_++;
        uint32_t index = 0;
        if (pos_ - start > MAX_INDEX_DIGITS || pos_ >= text_.size() ||
            !parseDecimal(text_.substr(start, pos_ - start), MAX_INDEX, index)) {
            return false;
        }
        const std::vector<FieldSpec>& specs = isArg ? args_.fields : *scope.specs;
        const std::vector<FieldValue>& values = isArg ? argValues_.values() : *scope.values;
        if (index >= specs.size()) return false;
        const FieldSpec& spec = specs[index];
        const char next = text_[pos_++];
        if (next == '*') {
            return !isArg && scope.top && spec.type == FieldType::GROUP_ROWS && rows(out, absent);
        }
        if (next != '}' || spec.type == FieldType::GROUP_ROWS) return false;
        if (index >= values.size() || !values[index].present) {
            absent = true;
            return true;
        }
        std::string text;
        if (!fieldToText(spec, values[index], text)) return false;
        out += text;
        return true;
    }

    /** Renders the rows of the group. The position is behind the '*' that starts the row text. */
    bool rows(std::string& out, bool& absent) {
        const size_t rowStart = pos_;
        const std::vector<std::vector<FieldValue>>& all = values_.rows();
        const std::vector<FieldValue> none;
        std::vector<std::string> rendered;
        // The row text is read at least once, so a template is checked also without rows.
        size_t row = 0;
        do {
            pos_ = rowStart;
            std::string text;
            bool rowAbsent = false;
            const Scope scope{&reply_.members, row < all.size() ? &all[row] : &none, false};
            if (!sequence(scope, "*}", text, rowAbsent) || pos_ >= text_.size()) return false;
            if (row < all.size()) rendered.push_back(text);
            row++;
        } while (row < all.size());

        std::string separator;
        if (text_[pos_] == '*') {
            pos_++;
            while (pos_ < text_.size() && text_[pos_] != '}') {
                if (text_[pos_] == '\\') pos_++;
                if (pos_ >= text_.size()) return false;
                separator += text_[pos_++];
            }
        }
        if (pos_ >= text_.size()) return false;
        pos_++;
        if (rendered.empty()) absent = true;
        for (size_t i = 0; i < rendered.size(); i++) {
            if (i > 0) out += separator;
            out += rendered[i];
        }
        return true;
    }

    const std::string& text_;
    const Schema& reply_;
    const Fields& values_;
    const Schema& args_;
    const Fields& argValues_;
    size_t pos_ = 0;
};

}  // namespace

bool parseSchema(const std::string& text, Schema& out) {
    Schema schema;
    size_t pos = 0;
    if (!text.empty() && !parseFieldList(text, pos, false, schema.fields, schema.members)) {
        return false;
    }
    out = schema;
    return true;
}

FieldValue& Fields::slot(size_t index) {
    if (index >= values_.size()) values_.resize(index + 1);
    return values_[index];
}

const FieldValue* Fields::value(size_t index) const {
    return index < values_.size() ? &values_[index] : nullptr;
}

bool Fields::has(size_t index) const {
    const FieldValue* found = value(index);
    return found != nullptr && found->present;
}

uint32_t Fields::u(size_t index) const {
    return has(index) ? static_cast<uint32_t>(values_[index].number) : 0;
}

int32_t Fields::i(size_t index) const {
    return has(index) ? static_cast<int32_t>(values_[index].number) : 0;
}

const std::vector<uint8_t>& Fields::bytes(size_t index) const {
    static const std::vector<uint8_t> none;
    return has(index) ? values_[index].bytes : none;
}

std::string Fields::text(size_t index) const {
    const std::vector<uint8_t>& data = bytes(index);
    return std::string(data.begin(), data.end());
}

Fields& Fields::setU(size_t index, uint32_t value) {
    FieldValue& field = slot(index);
    field = FieldValue();
    field.present = true;
    field.number = value;
    return *this;
}

Fields& Fields::setI(size_t index, int32_t value) {
    FieldValue& field = slot(index);
    field = FieldValue();
    field.present = true;
    field.number = value;
    return *this;
}

Fields& Fields::setBytes(size_t index, const uint8_t* data, size_t size) {
    FieldValue& field = slot(index);
    field = FieldValue();
    if (data != nullptr && size > 0) {
        field.present = true;
        field.bytes.assign(data, data + size);
    }
    return *this;
}

Fields& Fields::setText(size_t index, const std::string& value) {
    FieldValue& field = slot(index);
    field = FieldValue();
    field.present = !value.empty();
    field.bytes.assign(value.begin(), value.end());
    return *this;
}

Fields& Fields::setVersion(size_t index, uint8_t major, uint8_t minor, uint8_t patch) {
    const uint8_t parts[FIELD_VERSION_BYTES] = {major, minor, patch};
    return setBytes(index, parts, FIELD_VERSION_BYTES);
}

Fields& Fields::setValue(size_t index, const FieldValue& value) {
    slot(index) = value;
    return *this;
}

Fields& Fields::addRow(const Fields& row) {
    rows_.push_back(row.values());
    return *this;
}

Fields Fields::row(size_t index) const {
    Fields out;
    if (index < rows_.size()) out.values_ = rows_[index];
    return out;
}

void appendVarint(std::vector<uint8_t>& out, uint64_t value) {
    while (value >= VARINT_MORE) {
        out.push_back(static_cast<uint8_t>((value & (VARINT_MORE - 1U)) | VARINT_MORE));
        value >>= VARINT_BITS;
    }
    out.push_back(static_cast<uint8_t>(value));
}

bool readVarint(const uint8_t* data, size_t size, size_t& pos, uint64_t max, uint64_t& out) {
    uint64_t value = 0;
    for (size_t i = 0; i < VARINT_MAX_BYTES; i++) {
        if (data == nullptr || pos + i >= size) return false;
        const uint8_t byte = data[pos + i];
        value |= static_cast<uint64_t>(byte & (VARINT_MORE - 1U)) << (VARINT_BITS * i);
        if ((byte & VARINT_MORE) != 0) continue;
        // A last byte of zero after other bytes is a longer form of a smaller number.
        if ((byte == 0 && i > 0) || value > max) return false;
        pos += i + 1;
        out = value;
        return true;
    }
    return false;
}

bool encodeFields(const Schema& schema, const Fields& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> bytes;
    // Bytes up to the last value. Absent optional fields behind it are left out.
    size_t keep = 0;
    for (size_t i = 0; i < schema.fields.size(); i++) {
        const FieldSpec& spec = schema.fields[i];
        if (spec.type == FieldType::GROUP_ROWS) {
            for (const std::vector<FieldValue>& row : values.rows()) {
                for (size_t m = 0; m < schema.members.size(); m++) {
                    const FieldValue* member = m < row.size() ? &row[m] : nullptr;
                    if (!encodeScalar(schema.members[m], member, bytes)) return false;
                }
                keep = bytes.size();
            }
            continue;
        }
        if (!encodeScalar(spec, values.value(i), bytes)) return false;
        if (values.has(i)) keep = bytes.size();
    }
    bytes.resize(keep);
    out.insert(out.end(), bytes.begin(), bytes.end());
    return true;
}

bool decodeFields(const Schema& schema, const uint8_t* data, size_t size, Fields& out) {
    if (data == nullptr && size > 0) return false;
    Fields fields;
    size_t pos = 0;
    for (size_t i = 0; i < schema.fields.size(); i++) {
        const FieldSpec& spec = schema.fields[i];
        if (spec.type == FieldType::GROUP_ROWS) {
            while (pos < size) {
                Fields row;
                for (size_t m = 0; m < schema.members.size(); m++) {
                    FieldValue value;
                    if (pos >= size || !decodeScalar(schema.members[m], data, size, pos, value)) {
                        return false;
                    }
                    row.setValue(m, value);
                }
                fields.addRow(row);
            }
            continue;
        }
        FieldValue value;
        if (pos >= size) {
            if (!spec.optional) return false;
        } else if (!decodeScalar(spec, data, size, pos, value)) {
            return false;
        }
        fields.setValue(i, value);
    }
    out = fields;
    return true;
}

bool fieldToText(const FieldSpec& spec, const FieldValue& value, std::string& out) {
    if (!value.present) return false;
    if (isNumber(spec.type)) {
        uint64_t raw = 0;
        if (!value.bytes.empty() || !numberToWire(spec, value.number, raw)) return false;
        if (spec.type == FieldType::ENUM_NAME) {
            out = spec.names[static_cast<size_t>(value.number)];
            return true;
        }
        const bool negative = value.number < 0;
        const uint64_t magnitude = negative ? static_cast<uint64_t>(-(value.number + 1)) + 1
                                            : static_cast<uint64_t>(value.number);
        out = decimalText(magnitude, spec.decimals, negative);
        return true;
    }
    switch (spec.type) {
        case FieldType::HEX_BYTES:
        case FieldType::VERSION_BYTES:
            if (value.bytes.size() != fixedSize(spec)) return false;
            break;
        case FieldType::TEXT_REST:
        case FieldType::BYTES_REST:
            break;
        default:
            return false;
    }
    if (spec.type == FieldType::TEXT_REST) {
        out.assign(value.bytes.begin(), value.bytes.end());
    } else if (spec.type == FieldType::VERSION_BYTES) {
        out = std::to_string(static_cast<unsigned>(value.bytes[0])) + "." +
              std::to_string(static_cast<unsigned>(value.bytes[1])) + "." +
              std::to_string(static_cast<unsigned>(value.bytes[2]));
    } else {
        out = toHex(value.bytes.data(), value.bytes.size());
    }
    return true;
}

bool fieldFromText(const FieldSpec& spec, const std::string& text, FieldValue& out) {
    FieldValue value;
    value.present = true;
    switch (spec.type) {
        case FieldType::NUMBER_U:
        case FieldType::NUMBER_I:
            if (!numberFromText(spec, text, value.number)) return false;
            break;
        case FieldType::ENUM_NAME:
            if (!enumFromText(spec, text, value.number)) return false;
            break;
        case FieldType::HEX_BYTES:
            if (!fromHex(text, value.bytes) || value.bytes.size() != spec.size) return false;
            break;
        case FieldType::VERSION_BYTES:
            if (!versionFromText(text, value.bytes)) return false;
            break;
        case FieldType::TEXT_REST:
            if (text.empty() || text.find('\0') != std::string::npos) return false;
            value.bytes.assign(text.begin(), text.end());
            break;
        case FieldType::BYTES_REST:
            if (text.empty() || !fromHex(text, value.bytes)) return false;
            break;
        case FieldType::GROUP_ROWS:
            return false;
    }
    out = value;
    return true;
}

bool parseArgs(const Schema& schema, const std::string& args, Fields& out) {
    if (schema.hasGroup()) return false;
    size_t end = args.size();
    while (end > 0 && args[end - 1] == ' ') end--;
    size_t pos = 0;
    Fields fields;
    for (size_t i = 0; i < schema.fields.size(); i++) {
        const FieldSpec& spec = schema.fields[i];
        while (pos < end && args[pos] == ' ') pos++;
        FieldValue value;
        if (pos >= end) {
            if (!spec.optional) return false;
        } else {
            size_t wordEnd = end;
            if (spec.type != FieldType::TEXT_REST) {
                wordEnd = args.find(' ', pos);
                if (wordEnd == std::string::npos || wordEnd > end) wordEnd = end;
            }
            if (!fieldFromText(spec, args.substr(pos, wordEnd - pos), value)) return false;
            pos = wordEnd;
        }
        fields.setValue(i, value);
    }
    while (pos < end && args[pos] == ' ') pos++;
    if (pos != end) return false;
    out = fields;
    return true;
}

bool canonicalLine(const std::string& name, const Schema& schema, const Fields& values,
                   std::string& out) {
    if (schema.hasGroup()) return false;
    std::string line = name;
    bool ended = false;
    for (size_t i = 0; i < schema.fields.size(); i++) {
        const FieldSpec& spec = schema.fields[i];
        if (!values.has(i)) {
            if (!spec.optional) return false;
            ended = true;
            continue;
        }
        std::string text;
        if (ended || !fieldToText(spec, *values.value(i), text)) return false;
        line += " " + text;
    }
    out = line;
    return true;
}

bool isCanonicalLine(const std::string& line, const std::string& name, const Schema& schema) {
    if (line.compare(0, name.size(), name) != 0) return false;
    const std::string rest = line.substr(name.size());
    if (!rest.empty() && rest[0] != ' ') return false;
    Fields typed;
    std::vector<uint8_t> bytes;
    Fields decoded;
    std::string rebuilt;
    return parseArgs(schema, rest, typed) && encodeFields(schema, typed, bytes) &&
           decodeFields(schema, bytes.data(), bytes.size(), decoded) &&
           canonicalLine(name, schema, decoded, rebuilt) && rebuilt == line;
}

std::string usageFromSchema(const Schema& schema) {
    std::string text;
    for (const FieldSpec& spec : schema.fields) {
        if (!text.empty()) text += " ";
        const std::string inner =
            spec.type == FieldType::ENUM_NAME ? enumUsage(spec.names) : spec.name;
        text += spec.optional ? "[" + inner + "]" : "<" + inner + ">";
    }
    return text;
}

bool parseVersionPrefix(const std::string& text, uint8_t out[FIELD_VERSION_BYTES]) {
    uint8_t parts[FIELD_VERSION_BYTES] = {};
    size_t pos = 0;
    for (size_t part = 0; part < FIELD_VERSION_BYTES; part++) {
        if (part > 0) {
            if (pos >= text.size() || text[pos] != '.') return false;
            pos++;
        }
        const size_t start = pos;
        while (pos < text.size() && isDigit(text[pos])) pos++;
        uint32_t value = 0;
        if (pos - start > MAX_INDEX_DIGITS ||
            !parseDecimal(text.substr(start, pos - start), BYTE_MAX, value)) {
            return false;
        }
        parts[part] = static_cast<uint8_t>(value);
    }
    for (size_t part = 0; part < FIELD_VERSION_BYTES; part++) out[part] = parts[part];
    return true;
}

bool renderTemplate(const std::string& text, const Schema& reply, const Fields& values,
                    const Schema& args, const Fields& argValues, std::string& out) {
    return TemplateRenderer(text, reply, values, args, argValues).run(out);
}

bool checkTemplate(const std::string& text, const Schema& reply, const Schema& args) {
    std::string unused;
    return renderTemplate(text, reply, Fields(), args, Fields(), unused);
}
