#pragma once

#include "value.h"
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace redis_plugin {

struct Settings {
    std::string host = "127.0.0.1";
    int port = 6379;
    int database = 0;
    int connectTimeout = 1000;
    int commandTimeout = 1000;
    std::string username, password, clientName;
    bool tls = false;
    std::string caFile, certFile, keyFile;
    void validate() const;
};

struct SetOptions {
    std::optional<std::string> ttlMilliseconds;
    std::string condition;
    bool keepTTL = false;
    void append(std::vector<std::string>& arguments) const;
};

struct Reply {
    enum class Type { string, status, integer, null_value };
    Type type;
    std::string value;
};

struct ConnectionInfo {
    Settings settings;
    bool connected;
};

class Client {
public:
    explicit Client(const Settings& settings);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Reply command(const std::vector<std::string>& arguments);
    bool connected() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Registry {
public:
    Registry();
    ~Registry();
    int open(int owner, const Settings& settings);
    void close(int owner, int id);
    ConnectionInfo info(int owner, int id);
    Reply run(int owner, int id, const std::vector<std::string>& arguments);
    void close_owner(int owner);
    void clear();
private:
    struct Entry {
        int owner;
        Settings publicSettings;
        std::unique_ptr<Client> client;
    };
    Entry& get(int owner, int id);
    std::mutex mutex_;
    std::unordered_map<int, Entry> entries_;
    std::int64_t next_ = 1;
};

bool transport_runtime_ready();
std::vector<std::string> set_arguments(std::string key, std::string value,
                                      const SetOptions& options);
void expect(const Reply& reply, Reply::Type type);
}
