#include "client.h"

#include <limits>

namespace redis_plugin {

void Settings::validate() const {
    auto safe = [](const std::string& value) {
        return value.find('\0') == std::string::npos;
    };
    if (host.empty() || !safe(host) || port < 1 || port > 65535 ||
        database < 0 || connectTimeout < 1 || connectTimeout > 60000 ||
        commandTimeout < 1 || commandTimeout > 60000)
        throw Error("invalid_arguments", "Invalid host, port, database or timeout (1..60000 ms).");
    if (!safe(caFile) || !safe(certFile) || !safe(keyFile))
        throw Error("invalid_arguments", "TLS paths must not contain NUL.");
    if ((!tls && (!caFile.empty() || !certFile.empty() || !keyFile.empty())) ||
        (certFile.empty() != keyFile.empty()))
        throw Error("invalid_arguments", "TLS files require tls:true; certFile and keyFile must be paired.");
    if (!username.empty() && password.empty())
        throw Error("invalid_arguments", "An ACL username requires a nonempty password.");
}

void SetOptions::append(std::vector<std::string>& arguments) const {
    if (ttlMilliseconds) positive_decimal(*ttlMilliseconds);
    if (ttlMilliseconds && keepTTL)
        throw Error("invalid_arguments", "ttlMilliseconds and keepTTL are incompatible.");
    if (!condition.empty() && condition != "NX" && condition != "XX")
        throw Error("invalid_arguments", "condition must be NX or XX.");
    if (ttlMilliseconds) {
        arguments.emplace_back("PX");
        arguments.push_back(*ttlMilliseconds);
    }
    if (keepTTL) arguments.emplace_back("KEEPTTL");
    if (!condition.empty()) arguments.push_back(condition);
}

std::vector<std::string> set_arguments(std::string key, std::string value,
                                      const SetOptions& options) {
    std::vector<std::string> args{"SET", std::move(key), std::move(value)};
    options.append(args);
    return args;
}

void expect(const Reply& reply, Reply::Type type) {
    if (reply.type != type)
        throw Error("unexpected_reply", "Unexpected reply type from Redis.", "received", false);
}

Registry::Registry() { transport_runtime_ready(); }
Registry::~Registry() { clear(); }

Registry::Entry& Registry::get(int owner, int id) {
    auto it = entries_.find(id);
    if (it == entries_.end())
        throw Error("invalid_handle", "Unknown or closed connection handle.", "not_submitted", false);
    if (it->second.owner != owner)
        throw Error("wrong_owner", "Connection belongs to a different 4D process.", "not_submitted", false);
    return it->second;
}

int Registry::open(int owner, const Settings& settings) {
    settings.validate();
    auto client = std::make_unique<Client>(settings);
    Settings publicSettings = settings;
    publicSettings.username.clear();
    publicSettings.password.clear();
    // Paths can reveal local account names; connection info only returns endpoint and timings.
    publicSettings.caFile.clear();
    publicSettings.certFile.clear();
    publicSettings.keyFile.clear();
    publicSettings.clientName.clear();
    std::lock_guard lock(mutex_);
    if (next_ > std::numeric_limits<std::int32_t>::max())
        throw Error("handle_exhausted", "Connection ID space exhausted; restart the runtime.", "not_submitted", false);
    int id = static_cast<int>(next_++);
    entries_.emplace(id, Entry{owner, std::move(publicSettings), std::move(client)});
    return id;
}

void Registry::close(int owner, int id) {
    std::lock_guard lock(mutex_);
    get(owner, id);
    entries_.erase(id);
}

ConnectionInfo Registry::info(int owner, int id) {
    std::lock_guard lock(mutex_);
    auto& entry = get(owner, id);
    return {entry.publicSettings, entry.client->connected()};
}

Reply Registry::run(int owner, int id, const std::vector<std::string>& arguments) {
    std::lock_guard lock(mutex_);
    return get(owner, id).client->command(arguments);
}

void Registry::close_owner(int owner) {
    std::lock_guard lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.owner == owner) it = entries_.erase(it);
        else ++it;
    }
}

void Registry::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
}
}
