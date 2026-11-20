#include "cmdCatalogue.h"

#include <algorithm>

#include "cmdStatus.h"
#include "cmdText.h"
#include "cmdTyped.h"

namespace {

/** Room kept in the first line for the page position, enough for "999/999". */
constexpr size_t POSITION_ROOM = 7;
constexpr size_t MAX_PAGE_DIGITS = 3;
constexpr uint32_t MAX_PAGE = 999;

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowered(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

std::string headerStart(const std::string& version) {
    std::string clean = CommandCatalogue::field(version);
    std::replace(clean.begin(), clean.end(), ' ', '_');
    return "cmds " + std::to_string(CATALOGUE_FORMAT_VERSION) + " " + clean + " ";
}

}  // namespace

const char* commandPermName(Perm perm) {
    switch (perm) {
        case Perm::OPEN:
            return "open";
        case Perm::SIGNED:
            return "signed";
        case Perm::LOCAL_ONLY:
            break;
    }
    return "local";
}

bool sameCommandName(const std::string& a, const std::string& b) {
    return lowered(a) == lowered(b);
}

bool CommandCatalogue::add(const CommandSpec& spec) {
    if (spec.name.empty() || find(spec.name) != nullptr) return false;
    for (size_t i = 0; i < spec.codes.size(); i++) {
        if (spec.codes[i].code < COMMAND_STATUS_SPECIFIC_FIRST) return false;
        for (size_t j = 0; j < i; j++) {
            if (spec.codes[j].code == spec.codes[i].code) return false;
        }
    }
    if (spec.id != 0) {
        TypedDeclaration declaration;
        if (findById(spec.id) != nullptr ||
            !parseTypedDeclaration(spec.id, spec.args, spec.reply, spec.replyTemplate,
                                   declaration)) {
            return false;
        }
    } else if (!spec.args.empty() || !spec.reply.empty() || !spec.replyTemplate.empty()) {
        return false;
    }
    specs_.push_back(spec);
    return true;
}

const CommandSpec* CommandCatalogue::find(const std::string& name) const {
    for (const CommandSpec& spec : specs_) {
        if (sameCommandName(spec.name, name)) return &spec;
    }
    return nullptr;
}

const CommandSpec* CommandCatalogue::findById(uint8_t id) const {
    if (id == 0) return nullptr;
    for (const CommandSpec& spec : specs_) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

std::string CommandCatalogue::codeText(const std::string& name, uint8_t code) const {
    const CommandSpec* spec = find(name);
    if (spec == nullptr) return "";
    for (const CommandCode& entry : spec->codes) {
        if (entry.code == code) return entry.text;
    }
    return "";
}

std::string CommandCatalogue::field(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c == '|') c = '/';
        if (c == ';') c = ',';
        if (c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

std::string CommandCatalogue::line(const CommandSpec& spec) {
    std::string text =
        field(spec.name) + "|" + commandPermName(spec.perm) + "|" + field(spec.usage) + "|";
    for (size_t i = 0; i < spec.codes.size(); i++) {
        if (i > 0) text += ";";
        text += std::to_string(static_cast<unsigned>(spec.codes[i].code)) + "=" +
                field(spec.codes[i].text);
    }
    if (spec.id != 0) {
        text += "|" + toHex(&spec.id, 1) + "|" + field(spec.args) + "|" + field(spec.reply) + "=" +
                field(spec.replyTemplate);
    }
    return text;
}

std::vector<std::string> CommandCatalogue::pages(const std::string& version,
                                                 size_t maxPageBytes) const {
    std::vector<const CommandSpec*> sorted;
    sorted.reserve(specs_.size());
    for (const CommandSpec& spec : specs_) sorted.push_back(&spec);
    std::sort(sorted.begin(), sorted.end(), [](const CommandSpec* a, const CommandSpec* b) {
        std::string left = lowered(a->name);
        std::string right = lowered(b->name);
        return left != right ? left < right : a->name < b->name;
    });

    // Bodies first: the first line of each page needs the number of pages.
    const std::string start = headerStart(version);
    const size_t headerSize = start.size() + POSITION_ROOM;
    std::vector<std::string> bodies(1, "\n" + commandStatusCatalogueLine());
    size_t onPage = 0;
    for (const CommandSpec* spec : sorted) {
        std::string entry = "\n" + line(*spec);
        // A page always takes one command, also one that is too long for it.
        if (onPage > 0 && headerSize + bodies.back().size() + entry.size() > maxPageBytes) {
            bodies.emplace_back();
            onPage = 0;
        }
        bodies.back() += entry;
        onPage++;
    }

    std::vector<std::string> out;
    out.reserve(bodies.size());
    for (size_t i = 0; i < bodies.size(); i++) {
        out.push_back(start + std::to_string(i + 1) + "/" + std::to_string(bodies.size()) +
                      bodies[i]);
    }
    return out;
}

bool parseCataloguePage(const std::string& args, size_t pages, size_t& page) {
    if (args.empty()) {
        page = 1;
        return pages >= 1;
    }
    uint32_t value = 0;
    if (args.size() > MAX_PAGE_DIGITS || !parseDecimal(args, MAX_PAGE, value)) return false;
    if (value < 1 || value > pages) return false;
    page = value;
    return true;
}
