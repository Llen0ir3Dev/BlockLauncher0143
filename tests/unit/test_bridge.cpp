#include "../test_support.hpp"

#include <chrono>
#include <thread>

#include <nexus/bridge/agent.hpp>
#include <nexus/bridge/client.hpp>
#include <nexus/bridge/protocol.hpp>
#include <nexus/core/fs.hpp>

using namespace nexus_test;

namespace {

using namespace nexus::bridge;

void test_protocol_roundtrip() {
    const auto request = make_request("7", "shader.translate", {{"target", "gles100"}});
    const std::string line = encode_message(request);
    CHECK(!line.empty());
    CHECK(line.back() == '\n');

    BridgeMessage decoded;
    BridgeError error;
    CHECK(decode_message(line.substr(0, line.size() - 1), decoded, error));
    CHECK(decoded.kind == MessageKind::Request);
    CHECK_EQ(decoded.id, std::string("7"));
    CHECK_EQ(decoded.method, std::string("shader.translate"));
    CHECK_EQ(decoded.params.value("target", ""), std::string("gles100"));

    const auto response = make_response("7", {{"success", true}});
    BridgeMessage decoded_response;
    CHECK(decode_message(encode_message(response).substr(0, encode_message(response).size() - 1),
                         decoded_response, error));
    CHECK(decoded_response.kind == MessageKind::Response);
    CHECK(decoded_response.result.value("success", false));

    const auto failure = make_error_response(
        "8", error_code::kMethodNotFound, "unknown method");
    BridgeMessage decoded_error;
    CHECK(decode_message(encode_message(failure).substr(0, encode_message(failure).size() - 1),
                         decoded_error, error));
    CHECK(decoded_error.kind == MessageKind::Error);
    CHECK_EQ(decoded_error.error.code, error_code::kMethodNotFound);

    BridgeMessage ignored;
    CHECK(!decode_message("{not json", ignored, error));
    CHECK_EQ(error.code, error_code::kParse);
}

void test_agent_client_roundtrip() {
    const std::string directory = make_temp("nexus-bridge-test");
    BridgeAgentOptions agent_options;
    agent_options.socket_path = nexus::fs::join(directory, "bridge.sock");
    agent_options.max_clients = 2;
    BridgeAgent agent(agent_options);
    agent.register_method(
        "echo",
        [](const std::string&, const nlohmann::json& params, nlohmann::json& result,
           BridgeError&, const BridgeRequestContext& context) {
            result = {{"echo", params.value("value", "")},
                      {"requestId", context.request_id}};
            return true;
        });
    agent.register_method(
        "fail",
        [](const std::string&, const nlohmann::json&, nlohmann::json& result,
           BridgeError& error, const BridgeRequestContext&) {
            result = nlohmann::json::object();
            error.code = error_code::kDenied;
            error.message = "denied by test";
            return false;
        });

    std::string error;
    CHECK(agent.start(error));
    if (!error.empty()) std::cerr << error << "\n";
    CHECK(agent.running());
    CHECK(nexus::fs::exists(agent_options.socket_path));

    BridgeClientOptions client_options;
    client_options.socket_path = agent_options.socket_path;
    client_options.timeout_ms = 2000;
    BridgeClient client(client_options);
    CHECK(client.connect(error));
    if (!error.empty()) std::cerr << error << "\n";
    CHECK(client.connected());

    std::string version;
    CHECK(client.ping(&version, error));
    CHECK(!version.empty());

    nlohmann::json result;
    BridgeError bridge_error;
    CHECK(client.call("echo", {{"value", "hello"}}, result, bridge_error));
    CHECK_EQ(result.value("echo", ""), std::string("hello"));
    CHECK(!result.value("requestId", "").empty());

    CHECK(!client.call("fail", nlohmann::json::object(), result, bridge_error));
    CHECK_EQ(bridge_error.code, error_code::kDenied);
    CHECK_EQ(bridge_error.message, std::string("denied by test"));

    CHECK(!client.call("missing.method", nlohmann::json::object(), result, bridge_error));
    CHECK_EQ(bridge_error.code, error_code::kMethodNotFound);

    // Give the connection thread a moment to register, then publish an event.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(agent.client_count() >= 1);
    CHECK_EQ(agent.publish("test.event", {{"n", 1}}), static_cast<size_t>(1));
    BridgeMessage event;
    CHECK(client.poll_event(event, 2000, error));
    CHECK(event.kind == MessageKind::Event);
    CHECK_EQ(event.event, std::string("test.event"));
    CHECK_EQ(event.data.value("n", 0), 1);

    const auto stats = agent.stats();
    CHECK(stats.value("messagesReceived", 0) >= 3);
    CHECK(stats.value("running", false));

    client.disconnect();
    agent.stop();
    CHECK(!agent.running());
    CHECK(!nexus::fs::exists(agent_options.socket_path));
    nexus::fs::remove_all(directory);
}

void test_client_connect_failure() {
    BridgeClientOptions options;
    options.socket_path = nexus::fs::join(make_temp("nexus-bridge-missing"),
                                          "does-not-exist.sock");
    options.timeout_ms = 100;
    BridgeClient client(options);
    std::string error;
    CHECK(!client.connect(error));
    CHECK(!error.empty());
}

} // namespace

int main() {
    return run_all("bridge", {
        {"protocol roundtrip", test_protocol_roundtrip},
        {"agent/client roundtrip", test_agent_client_roundtrip},
        {"connect failure", test_client_connect_failure},
    });
}
