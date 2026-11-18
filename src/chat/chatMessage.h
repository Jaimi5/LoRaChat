#pragma once

#include "config.h"

#include "message/dataMessage.h"

#include "loramesh/loraMeshMessage.h"

#include "chatParser.h"

// A chat message is a plain DataMessage addressed to appPort::LoRaChat. Its payload
// (DataMessage::message, DataMessage::messageSize bytes) uses the wire format described in
// chatParser.h. There is no fragmentation, so the largest payload must fit in one LoRa packet
// together with the application header LoRaMeshService puts in front of it.
static_assert(CHAT_MAX_PAYLOAD + sizeof(LoRaMeshMessage) <= MAX_MSG_SIZE,
              "A chat message must fit in one LoRa packet");
