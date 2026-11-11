#include "client.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>

using namespace redis_plugin;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void error(const std::string& code, const std::function<void()>& action,
           const std::string& outcome = {}) {
    try { action(); }
    catch (const Error& e) {
        check(e.code == code, ("Unexpected code: " + e.code + " wanted " + code).c_str());
        if (!outcome.empty()) check(e.outcome == outcome, "Unexpected execution outcome");
        return;
    }
    throw std::runtime_error("Expected failure: " + code);
}

void units() {
    check(decimal("9223372036854775807") == std::numeric_limits<std::int64_t>::max(), "int64 max");
    check(decimal("-9223372036854775808") == std::numeric_limits<std::int64_t>::min(), "int64 min");
    for (auto s : {"9223372036854775808", "-9223372036854775809"})
        error("integer_overflow", [&] { decimal(s); });
    for (auto s : {"", "+1", "-0", "00", "01", " 1", "1 ", "1.0", "1e2"})
        error("invalid_arguments", [&] { decimal(s); });
    error("invalid_arguments", [] { positive_decimal("0"); });
    error("invalid_arguments", [] { positive_decimal("-1"); });
    auto wide = u"\u65e5\u672c\u8a9e\U0001f600";
    check(utf16(utf8(wide)) == wide, "Unicode round trip");
    std::u16string withNul{u'a', 0, u'b'};
    check(utf16(utf8(withNul)) == withNul, "Text NUL round trip");
    for (auto s : {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3),
                   std::string("\xf4\x90\x80\x80", 4), std::string("\x80", 1),
                   std::string("\xe3\x81", 2)})
        error("invalid_utf8", [&] { utf16(s); }, "received");
    error("invalid_arguments", [] { utf8(std::u16string{0xd800}); });
    error("invalid_arguments", [] { utf8(std::u16string{0xdc00}); });
    SetOptions set{std::string("1500"), "NX", false};
    auto args = set_arguments("k", std::string("\0\xff", 2), set);
    check(args == std::vector<std::string>({"SET", "k", std::string("\0\xff", 2), "PX", "1500", "NX"}), "SET atomic argv");
    error("invalid_arguments", [] { set_arguments("k", "v", {std::string("5"), "", true}); });
    error("invalid_arguments", [] { set_arguments("k", "v", {{}, "nx", false}); });
    check(set_arguments("k", "v", {{}, "XX", true}) ==
          std::vector<std::string>({"SET", "k", "v", "KEEPTTL", "XX"}), "SET XX KEEPTTL");
    Settings s;
    s.validate();
    s.commandTimeout = 0;
    error("invalid_arguments", [&] { s.validate(); });
    s.commandTimeout = 100;
    s.certFile = "certificate.pem";
    error("invalid_arguments", [&] { s.validate(); });
    Registry registry;
    error("invalid_handle", [&] { registry.close(1, 0); });
}

void transport(const std::string& mode, int port, const std::string& ca = {},
               const std::string& host = "127.0.0.1", const std::string& cert = {}, const std::string& key = {}) {
    Settings settings;
    settings.host = host;
    settings.port = port;
    settings.connectTimeout = 800;
    settings.commandTimeout = 150;
    settings.tls = !ca.empty() || mode == "open-fail-system";
    settings.caFile = ca;
    settings.certFile = cert;
    settings.keyFile = key;
    if (mode == "tls-config-fail") {
        error("tls_configuration", [&] { Client client(settings); }, "not_submitted");
        return;
    }
    if (mode == "setup") {
        settings.username = "cache-user";
        settings.password = std::string("test\0password", 13);
        settings.database = 3;
        settings.clientName = "4d:fixture";
    }
    if (mode == "open-fail" || mode == "open-fail-system") {
        error("open_failed", [&] { Client client(settings); }, "not_submitted");
        return;
    }
    if (mode == "open-timeout") {
        auto start = std::chrono::steady_clock::now();
        error("open_timeout", [&] { Client client(settings); }, "not_submitted");
        check(std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "Open deadline exceeded");
        return;
    }
    Registry registry;
    int id = registry.open(42, settings);
    check(registry.info(42, id).settings.port == port, "Local connection info");
    check(registry.info(42, id).settings.password.empty() && registry.info(42, id).settings.username.empty(),
          "Connection info credential redaction");
    error("wrong_owner", [&] { registry.run(43, id, {"PING"}); }, "not_submitted");
    if (mode == "timeout" || mode == "trickle" || mode == "write-timeout" ||
        mode == "disconnect" || mode == "push" || mode == "malformed") {
        auto start = std::chrono::steady_clock::now();
        auto code = (mode == "timeout" || mode == "trickle" || mode == "write-timeout") ? "command_timeout" :
                    mode == "push" ? "unsupported_push" : "connection_failure";
        auto value = mode == "write-timeout" ? std::string(8 * 1024 * 1024, 'v') : "v";
        error(code, [&] { registry.run(42, id, {"SET", "k", value}); }, "unknown");
        check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "Command deadline exceeded");
        error("connection_closed", [&] { registry.run(42, id, {"PING"}); }, "not_submitted");
        check(!registry.info(42, id).connected, "Local disconnected state");
    } else {
        auto reply = registry.run(42, id, {"GET", "missing"});
        check(reply.type == Reply::Type::null_value, "Missing key");
        reply = registry.run(42, id, {"GET", "empty"});
        check(reply.type == Reply::Type::string && reply.value.empty(), "Empty string");
        reply = registry.run(42, id, set_arguments("binary", std::string("\0\xff\r\n", 4), {std::string("5000"), "NX", false}));
        check(reply.type == Reply::Type::status && reply.value == "OK", "Binary SET");
        reply = registry.run(42, id, {"GET", "binary"});
        check(reply.value == std::string("\0\xff\r\n", 4), "Binary GET");
        error("redis_error", [&] { registry.run(42, id, {"INCRBY", "overflow", "1"}); }, "server_error");
        check(registry.run(42, id, {"PING"}).value == "PONG", "Reuse after Redis error");
        reply = registry.run(42, id, {"INCRBY", "counter", "1"});
        check(reply.type == Reply::Type::integer && reply.value == "9223372036854775807", "Exact int64");
    }
    registry.close(42, id);
    error("invalid_handle", [&] { registry.info(42, id); });
}

void real(int port) {
    Settings settings;
    settings.port = port;
    if (const char* password = std::getenv("REDIS_TEST_PASSWORD")) settings.password = password;
    settings.database = 1;
    settings.clientName = "4d-native-acceptance";
    if (const char* host = std::getenv("REDIS_TEST_HOST")) settings.host = host;
    if (const char* ca = std::getenv("REDIS_TEST_TLS_CA"); ca && *ca) {
        settings.caFile = ca;
        settings.tls = true;
    }
    Registry registry;
    int id = registry.open(10, settings);
    const std::string key = "4d-redis-native-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    registry.run(10, id, {"DEL", key});
    check(registry.run(10, id, {"GET", key}).type == Reply::Type::null_value, "Real miss");
    auto r = registry.run(10, id, set_arguments(key, "", {std::string("60000"), "NX", false}));
    check(r.value == "OK", "Real NX+PX");
    Settings defaultDatabase = settings;
    defaultDatabase.database = 0;
    int db0 = registry.open(10, defaultDatabase);
    check(registry.run(10, db0, {"GET", key}).type == Reply::Type::null_value, "Database selection");
    registry.close(10, db0);
    check(registry.run(10, id, {"GET", key}).value.empty(), "Real empty");
    check(registry.run(10, id, set_arguments(key, "no", {{}, "NX", false})).type == Reply::Type::null_value, "Real NX no-op");
    registry.run(10, id, set_arguments(key, std::string("\0\xff\0", 3), {{}, "XX", true}));
    check(registry.run(10, id, {"GET", key}).value == std::string("\0\xff\0", 3), "Real binary");
    check(decimal(registry.run(10, id, {"PTTL", key}).value) > 50000, "Real KEEPTTL");
    registry.run(10, id, {"SET", key, utf8(u"\u65e5\u672c\u8a9e\U0001f600")});
    check(utf16(registry.run(10, id, {"GET", key}).value) == u"\u65e5\u672c\u8a9e\U0001f600", "Real Unicode");
    for (auto boundary : {"9223372036854775807", "-9223372036854775808"}) {
        registry.run(10, id, {"SET", key, boundary});
        check(registry.run(10, id, {"INCRBY", key, "0"}).value == boundary, "Real int64 boundary");
        error("redis_error", [&] { registry.run(10, id, {"INCRBY", key, boundary[0] == '-' ? "-1" : "1"}); });
    }
    registry.run(10, id, {"DEL", key});
    registry.run(10, id, {"HSET", key, "field", "value"});
    error("redis_error", [&] { registry.run(10, id, {"GET", key}); });
    check(registry.run(10, id, {"PING"}).value == "PONG", "Real WRONGTYPE recovery");
    registry.run(10, id, {"DEL", key});
    registry.close_owner(10);
    error("invalid_handle", [&] { registry.info(10, id); });
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "units") units();
        else if (argc == 3 && std::string(argv[1]) == "real") real(std::stoi(argv[2]));
        else if (argc >= 3)
            transport(argv[1], std::stoi(argv[2]), argc > 3 ? argv[3] : "", argc > 4 ? argv[4] : "127.0.0.1",
                      argc > 5 ? argv[5] : "", argc > 6 ? argv[6] : "");
        else throw std::runtime_error("Expected mode and optional test server port");
        std::cout << "PASS\n";
        return 0;
    } catch (const Error& e) {
        std::cerr << "FAIL: " << e.code << " (" << e.outcome << "): " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
