#pragma once

#include <cstddef>
#include <string>

/**
 * @return the text of a received display message: the first @p size bytes of @p text, up to the
 *         first NUL. Senders may or may not include the NUL in the message size.
 */
std::string displayTextFrom(const char* text, size_t size);

/**
 * @return the longest text a display message carries when the radio takes @p maxPayload bytes
 *         per message and the text buffer holds @p capacity bytes. The command byte and the NUL
 *         take one byte each.
 */
size_t displayTextLimit(size_t maxPayload, size_t capacity);
