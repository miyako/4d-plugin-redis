#include "client.h"

#include <boost/redis/src.hpp>
#include <boost/redis.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <openssl/err.h>
#include <openssl/x509.h>

#ifdef __APPLE__
#include <Security/Security.h>
#elif defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace redis_plugin {
namespace {
namespace net = boost::asio;
namespace br = boost::redis;
using boost::system::error_code;

struct TransportRuntime {
    bool ready = OPENSSL_init_ssl(OPENSSL_INIT_NO_ATEXIT | OPENSSL_INIT_NO_LOAD_CONFIG, nullptr) == 1;
    ~TransportRuntime() { OPENSSL_cleanup(); }
};

int sni_index() {
    static int index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr,
        [](void*, void* value, CRYPTO_EX_DATA*, int, long, void*) {
            delete static_cast<std::string*>(value);
        });
    if (index < 0) throw Error("tls_configuration", "Unable to initialize TLS SNI.", "not_submitted", false);
    return index;
}

void configure_sni(net::ssl::context& context, const std::string& host) {
    auto name = std::make_unique<std::string>(host);
    if (SSL_CTX_set_ex_data(context.native_handle(), sni_index(), name.get()) != 1)
        throw Error("tls_configuration", "Unable to store TLS SNI.", "not_submitted", false);
    name.release();
    // Boost.Redis recreates SSL streams before connecting. Apply SNI to each new
    // SSL at handshake start, before OpenSSL constructs its ClientHello.
    SSL_CTX_set_info_callback(context.native_handle(), [](const SSL* ssl, int where, int) {
        if (where & SSL_CB_HANDSHAKE_START) {
            auto* host = static_cast<std::string*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), sni_index()));
            if (host) SSL_set_tlsext_host_name(const_cast<SSL*>(ssl), host->c_str());
        }
    });
}

void add_root(X509_STORE* store, const unsigned char* bytes, long size) {
    std::unique_ptr<X509, decltype(&X509_free)> cert(d2i_X509(nullptr, &bytes, size), X509_free);
    if (!cert) throw Error("tls_configuration", "Unable to decode a system trust anchor.", "not_submitted", false);
    if (X509_STORE_add_cert(store, cert.get()) != 1) {
        if (ERR_GET_REASON(ERR_peek_last_error()) != X509_R_CERT_ALREADY_IN_HASH_TABLE)
            throw Error("tls_configuration", "Unable to load a system trust anchor.", "not_submitted", false);
        ERR_clear_error();
    }
}

void system_roots(net::ssl::context& context) {
    auto* store = SSL_CTX_get_cert_store(context.native_handle());
#ifdef __APPLE__
    CFArrayRef anchors = nullptr;
    if (SecTrustCopyAnchorCertificates(&anchors) != errSecSuccess || !anchors)
        throw Error("tls_configuration", "Unable to read system trust anchors; specify caFile.", "not_submitted", false);
    std::unique_ptr<const void, decltype(&CFRelease)> roots(anchors, CFRelease);
    for (CFIndex i = 0; i < CFArrayGetCount(anchors); ++i) {
        auto certificate = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(anchors, i)));
        CFDataRef data = SecCertificateCopyData(certificate);
        if (!data) throw Error("tls_configuration", "Unable to read a trust anchor.", "not_submitted", false);
        std::unique_ptr<const void, decltype(&CFRelease)> owned(data, CFRelease);
        add_root(store, CFDataGetBytePtr(data), static_cast<long>(CFDataGetLength(data)));
    }
    if (CFArrayGetCount(anchors) == 0)
        throw Error("tls_configuration", "No system trust anchors; specify caFile.", "not_submitted", false);
#elif defined(_WIN32)
    HCERTSTORE roots = CertOpenSystemStoreW(0, L"ROOT");
    if (!roots) throw Error("tls_configuration", "Unable to read system trust anchors; specify caFile.", "not_submitted", false);
    struct Store {
        HCERTSTORE value;
        ~Store() { CertCloseStore(value, 0); }
    } owned{roots};
    PCCERT_CONTEXT cert = nullptr;
    bool any = false;
    while ((cert = CertEnumCertificatesInStore(roots, cert)) != nullptr) {
        try {
            add_root(store, cert->pbCertEncoded, static_cast<long>(cert->cbCertEncoded));
        } catch (...) {
            CertFreeCertificateContext(cert);
            throw;
        }
        any = true;
    }
    if (GetLastError() != static_cast<DWORD>(CRYPT_E_NOT_FOUND))
        throw Error("tls_configuration", "Unable to enumerate system trust anchors.", "not_submitted", false);
    if (!any) throw Error("tls_configuration", "No system trust anchors; specify caFile.", "not_submitted", false);
#endif
}

net::ssl::context tls_context(const Settings& settings) {
    if (!transport_runtime_ready())
        throw Error("tls_configuration", "Unable to initialize the private TLS library.", "not_submitted", false);
    net::ssl::context context(net::ssl::context::tls_client);
    if (!settings.tls) return context;
    try {
        if (SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_2_VERSION) != 1)
            throw Error("tls_configuration", "Unable to require TLS 1.2.", "not_submitted", false);
        context.set_password_callback([](std::size_t, net::ssl::context::password_purpose) {
            return std::string{};
        });
        if (settings.caFile.empty()) system_roots(context);
        else context.load_verify_file(settings.caFile);
        context.set_verify_mode(net::ssl::verify_peer);
        error_code addressError;
        net::ip::make_address(settings.host, addressError);
        bool dnsName = static_cast<bool>(addressError);
        if (dnsName) configure_sni(context, settings.host);
        context.set_verify_callback([host = settings.host, dnsName](bool verified, net::ssl::verify_context& ctx) {
            if (dnsName) {
                auto* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx.native_handle(), SSL_get_ex_data_X509_STORE_CTX_idx()));
                const char* sentName = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
                if (!sentName || host != sentName) return false;
            }
            return net::ssl::host_name_verification(host)(verified, ctx);
        });
        if (!settings.certFile.empty()) {
            context.use_certificate_chain_file(settings.certFile);
            context.use_private_key_file(settings.keyFile, net::ssl::context::pem);
            if (SSL_CTX_check_private_key(context.native_handle()) != 1)
                throw Error("tls_configuration", "TLS client certificate and key do not match.", "not_submitted", false);
        }
    } catch (const boost::system::system_error&) {
        throw Error("tls_configuration", "Unable to load TLS trust or client identity.", "not_submitted", false);
    }
    return context;
}
}

bool transport_runtime_ready() {
    // Registry constructs this before its own static initialization completes,
    // so registry/client cleanup precedes private OpenSSL teardown on unload.
    static TransportRuntime runtime;
    return runtime.ready;
}

struct Client::Impl {
    net::io_context io;
    br::config config;
    br::connection connection;
    net::cancellation_signal pushCancellation;
    br::generic_response pushes;
    bool usable = true;
    bool runDone = false;
    bool unsolicited = false;
    int commandTimeout;

    explicit Impl(const Settings& settings)
        : connection(io, tls_context(settings), br::logger{br::logger::level::disabled}),
          commandTimeout(settings.commandTimeout) {
        config.addr = {settings.host, std::to_string(settings.port)};
        config.use_ssl = settings.tls;
        config.resolve_timeout = std::chrono::milliseconds(settings.connectTimeout);
        config.connect_timeout = config.resolve_timeout;
        config.ssl_handshake_timeout = config.resolve_timeout;
        config.health_check_interval = std::chrono::seconds::zero();
        config.reconnect_wait_interval = std::chrono::seconds::zero();
        config.max_read_size = 64 * 1024 * 1024;
        config.use_setup = true;
        config.setup.clear();
        if (settings.password.empty()) config.setup.push("HELLO", "3");
        else config.setup.push("HELLO", "3", "AUTH",
                               settings.username.empty() ? "default" : settings.username,
                               settings.password);
        if (settings.database != 0) config.setup.push("SELECT", settings.database);
        if (!settings.clientName.empty()) config.setup.push("CLIENT", "SETNAME", settings.clientName);
        connection.set_receive_response(pushes);
        connection.async_receive2(net::bind_cancellation_slot(pushCancellation.slot(), [this](error_code ec) {
            if (!ec) {
                unsolicited = true;
                usable = false;
                connection.cancel();
            }
        }));
        connection.async_run(config, [this](error_code) {
            runDone = true;
            usable = false;
            connection.cancel(br::operation::exec);
        });
    }

    void stop() {
        usable = false;
        pushCancellation.emit(net::cancellation_type::all);
        connection.cancel();
        io.restart();
        io.run();
    }

    Reply execute(const std::vector<std::string>& arguments, int timeout, bool opening) {
        if (!usable)
            throw Error("connection_closed", "Connection is unusable; close it and open explicitly.", "not_submitted", false);
        if (arguments.empty()) throw Error("invalid_arguments", "Empty native request.");
        std::size_t argumentBytes = 0;
        for (const auto& arg : arguments) {
            if (arg.size() > 64 * 1024 * 1024 - argumentBytes)
                throw Error("invalid_arguments", "Request exceeds the 64 MiB payload limit.");
            argumentBytes += arg.size();
        }
        struct Operation {
            br::request request;
            br::generic_response response;
            net::steady_timer timer;
            bool done = false;
            bool timedOut = false;
            error_code error;
            explicit Operation(net::io_context& io) : timer(io) {}
        };
        auto op = std::make_shared<Operation>(io);
        const auto sentBefore = connection.get_usage().commands_sent;
        op->request.get_config().cancel_if_unresponded = true;
        if (arguments.size() == 1) op->request.push(arguments[0]);
        else op->request.push_range(arguments[0], arguments.begin() + 1, arguments.end());
        op->timer.expires_after(std::chrono::milliseconds(timeout));
        op->timer.async_wait([this, op](error_code ec) {
            if (!ec && !op->done) {
                op->timedOut = true;
                usable = false;
                connection.cancel();
            }
        });
        connection.async_exec(op->request, op->response, [op](error_code ec, std::size_t) {
            op->error = ec;
            op->done = true;
            op->timer.cancel();
        });
        io.restart();
        while (!op->done) {
            if (io.run_one() == 0) break;
        }
        if (op->timedOut || op->error || !op->done || unsolicited) {
            // Upstream increments commands_sent before attempting a write, not
            // after success. An increment therefore means possible submission.
            const bool attempted = connection.get_usage().commands_sent != sentBefore;
            stop();
            if (opening)
                throw Error(op->timedOut ? "open_timeout" : "open_failed",
                            "Unable to establish and initialize Redis connection.", "not_submitted", false);
            throw Error(unsolicited ? "unsupported_push" : (op->timedOut ? "command_timeout" : "connection_failure"),
                        attempted ? "Redis response unavailable; the command may already have executed. Do not retry blindly."
                                  : "Redis connection failed before request submission.",
                        attempted ? "unknown" : "not_submitted", false);
        }
        if (op->response.has_error()) {
            if (opening) {
                stop();
                throw Error("open_failed", "Redis rejected connection setup.", "not_submitted", false);
            }
            // Do not expose server-provided diagnostics, which may contain secrets or invalid UTF-8.
            throw Error("redis_error", "Redis rejected the command (type, ACL, value or integer overflow).",
                        "server_error", true);
        }
        const auto& nodes = op->response.value();
        if (nodes.size() != 1) {
            stop();
            throw Error("unexpected_reply", "Unexpected aggregate reply; connection discarded.", "received", false);
        }
        const auto& node = nodes.front();
        using Type = br::resp3::type;
        Reply reply;
        switch (node.data_type) {
            case Type::simple_string: reply = {Reply::Type::status, node.value}; break;
            case Type::blob_string: reply = {Reply::Type::string, node.value}; break;
            case Type::number:
                try { decimal(node.value); }
                catch (const Error&) {
                    stop();
                    throw Error("unexpected_reply", "Invalid Redis int64 reply.", "received", false);
                }
                reply = {Reply::Type::integer, node.value};
                break;
            case Type::null: reply = {Reply::Type::null_value, {}}; break;
            default:
                stop();
                throw Error("unexpected_reply", "Unsupported Redis reply; connection discarded.", "received", false);
        }
        bool valid;
        if (arguments[0] == "GET")
            valid = reply.type == Reply::Type::string || reply.type == Reply::Type::null_value;
        else if (arguments[0] == "SET")
            valid = reply.type == Reply::Type::null_value ||
                    (reply.type == Reply::Type::status && reply.value == "OK");
        else if (arguments[0] == "PING")
            valid = reply.type == Reply::Type::status && reply.value == "PONG";
        else
            valid = reply.type == Reply::Type::integer;
        if (!valid) {
            stop();
            throw Error("unexpected_reply", "Redis reply does not match the typed command.", "received", false);
        }
        return reply;
    }
};

Client::Client(const Settings& settings) {
    settings.validate();
    impl_ = std::make_unique<Impl>(settings);
    try {
        auto reply = impl_->execute({"PING"}, settings.connectTimeout, true);
        expect(reply, Reply::Type::status);
        if (reply.value != "PONG") throw Error("open_failed", "Invalid Redis handshake response.", "not_submitted", false);
    } catch (...) {
        impl_->stop();
        throw;
    }
}

Client::~Client() {
    if (impl_) impl_->stop();
}

Reply Client::command(const std::vector<std::string>& arguments) {
    return impl_->execute(arguments, impl_->commandTimeout, false);
}

bool Client::connected() const { return impl_->usable; }
}
