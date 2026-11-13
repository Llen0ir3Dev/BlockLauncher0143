#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus::bridge {

// Newline-delimited JSON protocol used between the NEXUS launcher and a
// bridge agent running alongside (or inside) a game session.
//
// Request   {"id":"1","method":"shader.reload","params":{...}}
// Response  {"id":"1","result":{...}}
// Failure   {"id":"1","error":{"code":-32601,"message":"unknown method"}}
// Event     {"event":"game.ready","data":{...}}
//
// One JSON object per line, UTF-8, at most kMaxMessageBytes bytes.  The
// protocol is intentionally small so it can be implemented by a native agent,
// an Android-side helper, or a test harness.
inline constexpr const char* kProtocolName = "nexus-bridge/1";
inline constexpr size_t kMaxMessageBytes = 8ull * 1024 * 1024;

enum class MessageKind { Request, Response, Error, Event, Unknown };
const char* message_kind_name(MessageKind kind);

struct BridgeError {
    int code = 0;
    std::string message;
    nlohmann::json data = nullptr;

    bool ok() const { return code == 0 && message.empty(); }
    nlohmann::json to_json() const;
    static BridgeError from_json(const nlohmann::json& json);
};

namespace error_code {
constexpr int kParse = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternal = -32603;
constexpr int kUnavailable = -32000;
constexpr int kTimeout = -32001;
constexpr int kDenied = -32002;
} // namespace error_code

struct BridgeMessage {
    MessageKind kind = MessageKind::Unknown;
    std::string id;
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json result = nlohmann::json::object();
    BridgeError error;
    std::string event;
    nlohmann::json data = nlohmann::json::object();
};

BridgeMessage make_request(const std::string& id, const std::string& method,
                           nlohmann::json params = nlohmann::json::object());
BridgeMessage make_response(const std::string& id, nlohmann::json result);
BridgeMessage make_error_response(const std::string& id, int code, const std::string& message,
                                  nlohmann::json data = nullptr);
BridgeMessage make_event(const std::string& event, nlohmann::json data = nlohmann::json::object());

// Encodes exactly one line (including the trailing newline).
std::string encode_message(const BridgeMessage& message);

// Parses one line without the trailing newline.  Returns false and fills
// `error` for malformed input; never throws.
bool decode_message(const std::string& line, BridgeMessage& message, BridgeError& error);

} // namespace nexus::bridge
