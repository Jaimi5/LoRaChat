#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cmdPerm.h"

/** Largest text of one /cmds page in bytes. With the request id the MQTT payload stays under 900.
 */
constexpr size_t CATALOGUE_PAGE_MAX_BYTES = 880;
/** Format version in the first line of every /cmds page. */
constexpr unsigned CATALOGUE_FORMAT_VERSION = 1;

/** An error that only one command gives: a status from 128 to 255 and its text for people. */
struct CommandCode {
    uint8_t code;
    std::string text;
};

/** What the catalogue tells about one command. */
struct CommandSpec {
    std::string name;
    Perm perm = Perm::LOCAL_ONLY;
    /** The arguments, without the command name. May be empty. */
    std::string usage;
    /** Time the command may take to run on the node, in ms. */
    uint32_t allowanceMs = 0;
    std::vector<CommandCode> codes;
    /** Id of the command in a binary frame. 0 for a command without the typed declaration. */
    uint8_t id = 0;
    /** Schema of the arguments, as cmdFields.h describes it. */
    std::string args;
    /** Schema of the reply fields. */
    std::string reply;
    /** Template that turns the reply fields into text. */
    std::string replyTemplate;
};

/** @return "open", "signed" or "local". */
const char* commandPermName(Perm perm);

/** @return @p a and @p b are the same command name. Case is ignored. */
bool sameCommandName(const std::string& a, const std::string& b);

/**
 * @brief The commands of the whole firmware, each name once.
 *
 * It tells a duplicate name or id at registration and writes the pages of /cmds.
 */
class CommandCatalogue {
public:
    /**
     * @brief Adds @p spec.
     * @return false, leaving the catalogue unchanged, if the name is empty or taken, a code is
     *         below 128 or listed twice, the id is taken, or the typed declaration is not valid
     *         (parseTypedDeclaration()). A command without an id has no schemas and no template.
     */
    bool add(const CommandSpec& spec);

    /** @return the command named @p name, or nullptr. */
    const CommandSpec* find(const std::string& name) const;

    /** @return the command with id @p id, or nullptr. Id 0 finds nothing. */
    const CommandSpec* findById(uint8_t id) const;

    /** @return the text of specific error @p code of command @p name, or empty. */
    std::string codeText(const std::string& name, uint8_t code) const;

    size_t size() const { return specs_.size(); }

    /**
     * @brief Writes every page of /cmds, commands sorted by name.
     *
     * Line 1 of a page is "cmds 1 <version> <page>/<pages>". Page 1 then lists the generic
     * errors. Each command takes one line "name|perm|usage|code=text;code=text". A command with
     * an id appends three fields: "|id|args|reply=template", the id in two hex digits, the two
     * schemas and the template as declared. A command without an id keeps four fields, which
     * keeps the pages small. The characters '|', ';' and newline never appear inside a field.
     * @param version firmware version. Its spaces become '_'.
     * @param maxPageBytes most bytes of one page.
     * @return the pages, at least one.
     */
    std::vector<std::string> pages(const std::string& version,
                                   size_t maxPageBytes = CATALOGUE_PAGE_MAX_BYTES) const;

    /** @return the line of @p spec in a /cmds page, without the newline. */
    static std::string line(const CommandSpec& spec);

    /** @return @p text as a field: '|' becomes '/', ';' becomes ',', a line break a space. */
    static std::string field(const std::string& text);

private:
    std::vector<CommandSpec> specs_;
};

/**
 * @brief Reads the argument of /cmds.
 * @param pages number of pages there are.
 * @param page gets the page asked for, from 1. No argument means page 1.
 * @return false if @p args is not a page number from 1 to @p pages.
 */
bool parseCataloguePage(const std::string& args, size_t pages, size_t& page);
