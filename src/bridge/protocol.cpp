#include <nexus/bridge/protocol.hpp>

#include <sstream>

namespace nexus::bridge {

const char* message_kind_name(MessageKind kind) {
    switch (kind) {
        case MessageKind::Request: return "request";
        case MessageKind::Response: return "response";
        case MessageKind::Error: return "error";
        case MessageKind::Event: return "event";
        case MessageKind::Unknown: break;
    }
    return "unknown";
}

nlohmann::json BridgeError::to_json() const {
    nlohmann::json out = {{"code", code}, {"message", message}};
    if (!data.is_null()) out["data"] = data;
    return out;
}

BridgeError BridgeError::from_json(const nlohmann::json& json) {
    BridgeError error;
    if (!json.is_object()) {
        error.code = error_code::kInvalidRequest;
        error.message = "error object is not a JSON object";
        return error;
    }
    const auto code = json.find("code");
    if (code != json.end() && code->is_number_integer()) error.code = code->get<int>();
    const auto message = json.find("message");
    if (message != json.end() && message->is_string()) error.message = message->get<std::string>();
    const auto data = json.find("data");
    if (data != json.end()) error.data = *data;
    if (error.message.empty()) error.message = "unspecified bridge error";
    return error;
}

BridgeMessage make_request(const std::string& id, const std::string& method,
                           nlohmann::json params) {
    BridgeMessage message;
    message.kind = MessageKind::Request;
    message.id = id;
    message.method = method;
    message.params = std::move(params);
    if (message.params.is_null()) message.params = nlohmann::json::object();
    return message;
}

BridgeMessage make_response(const std::string& id, nlohmann::json result) {
    BridgeMessage message;
    message.kind = MessageKind::Response;
    message.id = id;
    message.result = std::move(result);
    if (message.result.is_null()) message.result = nlohmann::json::object();
    return message;
}

BridgeMessage make_error_response(const std::string& id, int code, const std::string& message,
                                  nlohmann::json data) {
    BridgeMessage out;
    out.kind = MessageKind::Error;
    out.id = id;
    out.error.code = code;
    out.error.message = message;
    out.error.data = std::move(data);
    return out;
}

BridgeMessage make_event(const std::string& event, nlohmann::json data) {
    BridgeMessage message;
    message.kind = MessageKind::Event;
    message.event = event;
    message.data = std::move(data);
    if (message.data.is_null()) message.data = nlohmann::json::object();
    return message;
}

std::string encode_message(const BridgeMessage& message) {
    nlohmann::json json;
    switch (message.kind) {
        case MessageKind::Request:
            json = {{"id", message.id}, {"method", message.method}, {"params", message.params}};
            break;
        case MessageKind::Response:
            json = {{"id", message.id}, {"result", message.result}};
            break;
        case MessageKind::Error:
            json = {{"id", message.id}, {"error", message.error.to_json()}};
            break;
        case MessageKind::Event:
            json = {{"event", message.event}, {"data", message.data}};
            break;
        case MessageKind::Unknown:
            json = {{"error", {{"code", error_code::kInvalidRequest},
                               {"message", "unknown message kind"}}}};
            break;
    }
    return json.dump() + "\n";
}

bool decode_message(const std::string& line, BridgeMessage& message, BridgeError& error) {
    message = BridgeMessage{};
    error = BridgeError{};
    if (line.size() > kMaxMessageBytes) {
        error.code = error_code::kParse;
        error.message = "message exceeds the maximum size";
        return false;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(line);
    } catch (const std::exception& ex) {
        error.code = error_code::kParse;
        error.message = std::string("invalid JSON: ") + ex.what();
        return false;
    }
    if (!json.is_object()) {
        error.code = error_code::kInvalidRequest;
        error.message = "message must be a JSON object";
        return false;
    }
    if (json.contains("event")) {
        if (!json["event"].is_string()) {
            error.code = error_code::kInvalidRequest;
            error.message = "event name must be a string";
            return false;
        }
        message.kind = MessageKind::Event;
        message.event = json["event"].get<std::string>();
        message.data = json.value("data", nlohmann::json::object());
        return true;
    }
    if (json.contains("method")) {
        if (!json["method"].is_string()) {
            error.code = error_code::kInvalidRequest;
            error.message = "method must be a string";
            return false;
        }
        message.kind = MessageKind::Request;
        message.method = json["method"].get<std::string>();
        const auto id = json.find("id");
        if (id != json.end()) {
            if (id->is_string()) message.id = id->get<std::string>();
            else if (id->is_number_integer()) message.id = std::to_string(id->get<int64_t>());
            else if (!id->is_null()) {
                error.code = error_code::kInvalidRequest;
                error.message = "id must be a string or integer";
                return false;
            }
        }
        message.params = json.value("params", nlohmann::json::object());
        if (!message.params.is_object() && !message.params.is_null()) {
            error.code = error_code::kInvalidParams;
            error.message = "params must be a JSON object";
            return false;
        }
        if (message.params.is_null()) message.params = nlohmann::json::object();
        return true;
    }
    if (json.contains("result") || json.contains("error")) {
        message.kind = json.contains("error") ? MessageKind::Error : MessageKind::Response;
        const auto id = json.find("id");
        if (id != json.end()) {
            if (id->is_string()) message.id = id->get<std::string>();
            else if (id->is_number_integer()) message.id = std::to_string(id->get<int64_t>());
        }
        if (json.contains("error")) {
            message.error = BridgeError::from_json(json["error"]);
        } else {
            message.result = json["result"];
        }
        return true;
    }
    error.code = error_code::kInvalidRequest;
    error.message = "message has no event, method, result or error member";
    return false;
}

} // namespace nexus::bridge
