#pragma once

#include "ip_tools.h"
#include "lib_cereal.h"
#include "dream_command.h"
#include "dream_clock.h"

#include <string>
#include <atomic>
#include <list>
#include <queue>

constexpr size_t MAX_PAYLOAD_SIZE = 1024 * 1024 * 64; // fixed cache for incoming data

namespace dream {

class ClientObject {
    static std::atomic<uint64_t> _hook_id_counter;

    asio::io_context& ctx;
    asio::ip::tcp::socket socket;

    uint64_t id;
    std::string name;
    size_t consecutiveErrors;

    std::atomic_bool server_authorized, authorizing, valid;
    char authbuf[16] {}; // small buffer for authorization
    alignas(uint32_t) char cmdbuf[4]; // buffer for new incoming command data length data

    std::mutex outgoing_command_lock, shutdown_lock, runtime_command_lock;
    std::list<std::stringstream> out_data; // buffers for outgoing data
    char* in_data; // memory buffer for incoming data
    std::stringstream in_payload; // cache buffer for large payloads
    std::queue<Command> in_commands; // commands that are ready for processing

    // client hooks
    using HookCallback = std::function<void(ClientObject&, const std::any& data)>;

    std::map<std::string, std::map<uint64_t, HookCallback>> cb_hooks;

public:
    ClientObject(asio::io_context& ctx, asio::ip::tcp::socket&& soc, uint64_t id, std::string name):
        ctx(ctx), socket(std::move(soc)), id(id), name(name), consecutiveErrors(0),
        server_authorized(false), authorizing(false), valid(true), in_data(new char[MAX_PAYLOAD_SIZE]) {}

    ~ClientObject();

    ClientObject(const ClientObject&) = delete;
    ClientObject& operator=(const ClientObject&) = delete;

    void server_authorize(); // begin authorize process for server - asynchronous
    void client_authorize(); // begin authorize process for client - asynchronous

    void runtime_update(); // misc blocking update loop
    bool send_raw_data(const char* data, size_t length, std::function<void(bool)> on_complete=[](bool){});
    void send_command(const Command& cmd); // send command to socket

    void shutdown(); // a safe way to shutdown the socket
    bool is_valid();
    bool is_authorized();
    size_t get_id() { return id; }
    std::string get_name() { return name; }

    uint64_t register_hook(const std::string& hook_name, HookCallback hook);
    void unregister_hook(uint64_t id);

private:
    void incoming_command_handle(); // Command length payloads are async-retrieved via this basic retrieve method
    void incoming_data_handle(size_t length); // Command data payloads are async-retrieved via this basic retrieve method

    void process_command(Command& cmd);

    bool internal_error_check(const asio::error_code& error);

    void trigger_hook(const std::string& hook_name, const std::any& data = {});

public:
    template<typename Archive>
    void serialize(Archive& ar) {
        ar(id, name);
    }
};


}
