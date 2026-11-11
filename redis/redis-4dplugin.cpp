#include "4DPluginAPI.h"
#include "client.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace {
using namespace redis_plugin;
Registry registry;

std::string text(PA_Unistring* value) {
    if (!value) return {};
    auto length = PA_GetUnistringLength(value);
    auto* chars = PA_GetUnistring(value);
    std::u16string result;
    result.reserve(length);
    for (PA_long32 i = 0; i < length; ++i) result += static_cast<char16_t>(chars[i]);
    return utf8(result);
}

std::string argument(PA_PluginParameters params, short index) {
    return text(PA_GetStringParameter(params, index));
}

struct Uni {
    PA_Unistring value;
    explicit Uni(const std::string& ascii) {
        std::vector<PA_Unichar> buffer(ascii.begin(), ascii.end());
        buffer.push_back(0);
        value = PA_CreateUnistring(buffer.data());
    }
    ~Uni() { PA_DisposeUnistring(&value); }
};

struct Variable {
    PA_Variable value{};
    explicit Variable(PA_Variable v) : value(v) {}
    ~Variable() { PA_ClearVariable(&value); }
    Variable(const Variable&) = delete;
};

std::string string_value(PA_Variable value) {
    auto kind = PA_GetVariableKind(value);
    if (kind != eVK_Unistring && kind != eVK_Text)
        throw Error("invalid_arguments", "Expected Text.");
    auto str = PA_GetStringVariable(value);
    return text(&str);
}

class Options {
    PA_ObjectRef object_;
public:
    Options(PA_ObjectRef object, std::initializer_list<std::string> allowed) : object_(object) {
        if (!object) return;
        std::set<std::string> names(allowed);
        PA_Variable input{};
        PA_SetObjectVariable(&input, object);
        Variable keys(PA_ExecuteCommandByID(1719, &input, 1)); // OB Keys, since 18 R3.
        auto collection = PA_GetCollectionVariable(keys.value);
        if (!collection) throw Error("sdk_error", "Unable to enumerate settings.");
        for (PA_long32 i = 0; i < PA_GetCollectionLength(collection); ++i) {
            Variable key(PA_GetCollectionElement(collection, i));
            if (!names.count(string_value(key.value)))
                throw Error("invalid_arguments", "Unknown option property.");
        }
    }
    bool has(const std::string& name) const {
        if (!object_) return false;
        Uni key(name);
        return PA_HasObjectProperty(object_, &key.value);
    }
    PA_Variable get(const std::string& name) const {
        Uni key(name);
        return PA_GetObjectProperty(object_, &key.value);
    }
    std::string str(const std::string& name, std::string fallback = {}) const {
        if (!has(name)) return fallback;
        Variable v(get(name));
        return string_value(v.value);
    }
    bool boolean(const std::string& name, bool fallback = false) const {
        if (!has(name)) return fallback;
        Variable v(get(name));
        if (PA_GetVariableKind(v.value) != eVK_Boolean)
            throw Error("invalid_arguments", "Expected a Boolean option.");
        return PA_GetBooleanVariable(v.value) != 0;
    }
    int number(const std::string& name, int fallback) const {
        if (!has(name)) return fallback;
        Variable v(get(name));
        double n;
        auto kind = PA_GetVariableKind(v.value);
        if (kind == eVK_Longint) n = PA_GetLongintVariable(v.value);
        else if (kind == eVK_Real) n = PA_GetRealVariable(v.value);
        else throw Error("invalid_arguments", "Expected an integer Number option.");
        if (!std::isfinite(n) || std::trunc(n) != n ||
            n < 0 || n > std::numeric_limits<std::int32_t>::max())
            throw Error("invalid_arguments", "Number option is outside the nonnegative Longint range.");
        return static_cast<int>(n);
    }
    std::optional<std::string> ttl() const {
        if (!has("ttlMilliseconds")) return {};
        Variable v(get("ttlMilliseconds"));
        auto kind = PA_GetVariableKind(v.value);
        std::string result;
        if (kind == eVK_Unistring || kind == eVK_Text) result = string_value(v.value);
        else result = std::to_string(number("ttlMilliseconds", 0));
        positive_decimal(result);
        return result;
    }
};

Settings settings(PA_ObjectRef object) {
    Options o(object, {"host", "port", "database", "connectTimeout", "commandTimeout",
                      "username", "password", "clientName", "tls", "caFile", "certFile", "keyFile"});
    Settings s;
    s.host = o.str("host", s.host);
    s.port = o.number("port", s.port);
    s.database = o.number("database", s.database);
    s.connectTimeout = o.number("connectTimeout", s.connectTimeout);
    s.commandTimeout = o.number("commandTimeout", s.commandTimeout);
    s.username = o.str("username");
    s.password = o.str("password");
    s.clientName = o.str("clientName");
    s.tls = o.boolean("tls");
    s.caFile = o.str("caFile");
    s.certFile = o.str("certFile");
    s.keyFile = o.str("keyFile");
    s.validate();
    return s;
}

SetOptions set_options(PA_ObjectRef object) {
    Options o(object, {"ttlMilliseconds", "condition", "keepTTL"});
    auto condition = o.str("condition");
    if (o.has("condition") && condition.empty())
        throw Error("invalid_arguments", "condition must be NX or XX when supplied.");
    return {o.ttl(), std::move(condition), o.boolean("keepTTL")};
}

std::string quoted(const std::string& text) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (auto c : utf16(text)) {
        if (c >= 32 && c < 127 && c != '"' && c != '\\') out += static_cast<char>(c);
        else {
            out += "\\u";
            out += hex[(c >> 12) & 15];
            out += hex[(c >> 8) & 15];
            out += hex[(c >> 4) & 15];
            out += hex[c & 15];
        }
    }
    return out + '"';
}

class Result {
    std::string json_ = "{";
    void field(const std::string& name, const std::string& value) {
        if (json_.size() > 1) json_ += ',';
        json_ += quoted(name) + ':' + value;
    }
public:
    void str(const std::string& name, const std::string& value) { field(name, quoted(value)); }
    void boolean(const std::string& name, bool value) { field(name, value ? "true" : "false"); }
    void number(const std::string& name, int value) { field(name, std::to_string(value)); }
    void send(PA_PluginParameters params) {
        Uni json(json_ + '}');
        auto parsed = PA_JsonParse(&json.value, eVK_Object);
        auto object = PA_GetObjectVariable(parsed);
        if (!object) throw Error("sdk_error", "Unable to construct the result object.", "received");
        PA_ReturnObject(params, object);
    }
};

struct BlobOutput {
    PA_Variable value{};
    explicit BlobOutput(const std::string& bytes) {
        if (!bytes.empty()) {
            PA_SetBlobVariable(&value, const_cast<char*>(bytes.data()), static_cast<PA_long32>(bytes.size()));
            if (PA_GetLastError() != eER_NoErr) {
                PA_ClearVariable(&value);
                throw Error("sdk_error", "Unable to allocate output BLOB.", "received");
            }
        } else {
            PA_SetBlobVariable(&value, nullptr, 0);
        }
    }
    ~BlobOutput() { PA_ClearVariable(&value); }
    void commit(PA_PluginParameters params) {
        PA_SetBlobHandleParameter(params, 3, PA_GetBlobHandleVariable(value));
        value = {};
    }
};

std::string blob(PA_PluginParameters params, short index) {
    auto size = PA_GetBlobParameter(params, index, nullptr);
    if (size < 0) throw Error("invalid_arguments", "Invalid BLOB size.");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size) PA_GetBlobParameter(params, index, result.data());
    return result;
}

std::vector<std::string> keys(PA_CollectionRef collection, const std::string& command) {
    if (!collection) throw Error("invalid_arguments", "A nonempty Collection of Text keys is required.");
    auto count = PA_GetCollectionLength(collection);
    if (count < 1 || count > 10000)
        throw Error("invalid_arguments", "Supply between 1 and 10000 keys.");
    std::vector<std::string> args{command};
    for (PA_long32 i = 0; i < count; ++i) {
        Variable v(PA_GetCollectionElement(collection, i));
        args.push_back(string_value(v.value));
    }
    return args;
}

void dispatch(PA_long32 selector, PA_PluginParameters params) {
    auto owner = PA_GetCurrentProcessNumber();
    Result result;
    std::optional<BlobOutput> output;
    result.boolean("ok", true);
    if (selector == 1) {
        result.number("connection", registry.open(owner, settings(PA_GetObjectParameter(params, 1))));
    } else {
        auto id = PA_GetLongParameter(params, 1);
        if (selector == 2) registry.close(owner, id);
        else if (selector == 3) {
            auto [s, connected] = registry.info(owner, id);
            result.str("host", s.host);
            result.number("port", s.port);
            result.number("database", s.database);
            result.boolean("tls", s.tls);
            result.boolean("connected", connected);
            result.number("connectTimeout", s.connectTimeout);
            result.number("commandTimeout", s.commandTimeout);
        } else {
            std::vector<std::string> args;
            switch (selector) {
                case 4: case 6: args = {"GET", argument(params, 2)}; break;
                case 5: case 7:
                    args = set_arguments(argument(params, 2),
                                         selector == 5 ? argument(params, 3) : blob(params, 3),
                                         set_options(PA_GetObjectParameter(params, 4)));
                    break;
                case 8: case 9:
                    args = keys(PA_GetCollectionParameter(params, 2), selector == 8 ? "DEL" : "EXISTS");
                    break;
                case 10: {
                    auto delta = argument(params, 3);
                    decimal(delta);
                    args = {"INCRBY", argument(params, 2), delta};
                    break;
                }
                case 11: {
                    auto ttl = argument(params, 3);
                    positive_decimal(ttl);
                    args = {"PEXPIRE", argument(params, 2), ttl};
                    break;
                }
                case 12: args = {"PTTL", argument(params, 2)}; break;
                case 13: args = {"PERSIST", argument(params, 2)}; break;
                case 14: args = {"PING"}; break;
                default: throw Error("invalid_arguments", "Unknown plugin command.");
            }
            auto reply = registry.run(owner, id, args);
            switch (selector) {
                case 4: case 6: {
                    bool found = reply.type != Reply::Type::null_value;
                    if (found) expect(reply, Reply::Type::string);
                    result.boolean("found", found);
                    if (selector == 4 && found) result.str("value", reply.value);
                    if (selector == 6) {
                        if (reply.value.size() > static_cast<std::size_t>(std::numeric_limits<PA_long32>::max()))
                            throw Error("value_too_large", "BLOB exceeds the SDK Longint size limit.", "received");
                        output.emplace(found ? reply.value : std::string{});
                    }
                    break;
                }
                case 5: case 7:
                    if (reply.type != Reply::Type::null_value) expect(reply, Reply::Type::status);
                    result.boolean("applied", reply.type != Reply::Type::null_value);
                    break;
                case 11: case 13:
                    expect(reply, Reply::Type::integer);
                    result.boolean("applied", reply.value == "1");
                    break;
                case 14: expect(reply, Reply::Type::status); result.str("value", reply.value); break;
                default: expect(reply, Reply::Type::integer); result.str("value", reply.value); break;
            }
        }
    }
    result.send(params);
    if (output) output->commit(params);
}

void fail(PA_PluginParameters params, const Error& error, bool opening = false) noexcept {
    try {
        Result result;
        result.boolean("ok", false);
        result.str("code", error.code);
        result.str("message", error.what());
        result.str("outcome", error.outcome);
        result.boolean("connectionUsable", error.connectionUsable && !opening);
        result.send(params);
    } catch (const std::exception&) {
        std::fputs("redis: host/native allocation failed while constructing an error result\n", stderr);
        PA_ReturnObject(params, nullptr);
    }
}
}

void PluginMain(PA_long32 selector, PA_PluginParameters params) {
    if (selector == kClientDeinitPlugin || selector == kServerDeinitPlugin) {
        registry.clear();
        return;
    }
    if (selector == kCloseProcess || selector == kCreateProcess) {
        registry.close_owner(PA_GetCurrentProcessNumber());
        return;
    }
    if (selector < 1) return;
    try {
        dispatch(selector, params);
    } catch (const Error& e) {
        fail(params, e, selector == 1);
    } catch (const std::exception&) {
        // Never allow C++ exceptions or potentially secret library diagnostics across the SDK ABI.
        registry.close_owner(PA_GetCurrentProcessNumber());
        fail(params, Error("internal_error", "Native plugin failure.", "unknown", false));
    }
}
