#pragma once

#include "ip_tools.h"
#include "lib_cereal.h"
#include "dream_command.h"
#include "dream_clock.h"

#include <string>
#include <atomic>
#include <semaphore>
#include <list>
#include <fstream>
#include <queue>

namespace dream {

//constexpr size_t MAX_PAYLOAD_SIZE = 1024 * 1024 * 64; // fixed cache for incoming data
constexpr size_t MAX_PAYLOAD_SIZE = 256; // debug: ultra small cache size for forcing payload fragmentation

static const char DREAM_PROTO_ACCESS [128] = {"\x31\x08\x67\xb0\xca\x7b\xfc\xa2\x8a\x00\x9b\x68\x71\x62\xb4\xa1\x1f\x63\xe1\xe7\x61\x74\x24\x7a\x93\xbc\x30\xbf\x83\xad\xcf\x8d\x89\x5c\x44\xb6\x57\x4c\xc4\xd0\xb4\x0a\x7c\x8a\x6c\xbe\x58\x90\xac\x7c\xf8\x23\x33\x86\x6d\xcf\x49\xe2\x28\x9b\x49\x24\xd3\xb0\x5c\x71\xd8\xf0\x5c\xa6\x2b\xeb\x8c\x14\x19\x03\xfa\x64\x10\x78\x39\xc0\xdc\x64\xf1\x10\xe6\xa4\x53\xc8\x57\xb9\x71\xe3\xa7\x37\xd4\xbb\xca\xb1\x90\xfa\x7f\x8a\x8c\xd9\x6b\x15\xa4\xee\xf4\x7d\x07\x79\x28\xe5\x17\x57\xbb\x69\x83\x10\x7f\x1f\x49\xe0\xfc"};



class ClientObject {
    static std::atomic<uint64_t> _hook_id_counter;

    asio::io_context& ctx;
    asio::ip::tcp::socket socket;

    uint64_t id;
    std::string name;
    size_t consecutiveErrors;

    std::atomic_bool server_authorized, authorizing, valid;
    alignas(uint32_t) char cmdbuf[4]; // buffer for new incoming command data length data

    std::mutex outgoing_command_lock, shutdown_lock, runtime_command_lock;

    char* in_data; // memory buffer for incoming data
    std::stringstream in_payload; // cache buffer for large payloads
    std::stringstream out_payload; // cache buffer for new command packages while waiting for outgoing data flush
    std::stringstream out_data; // cache buffer for currently flushing / outgoing command packages

    std::queue<Command> in_commands, out_commands; // commands that are ready for processing - commands that are ready to send

    std::binary_semaphore in_payload_protection; // debug and protect read payloads from getting corrupt
    std::binary_semaphore out_payload_protection; // debug and protect write payloads from getting corrupt

    // Hookable interface

    using HookCallback = std::function<void(ClientObject&, const std::any& data)>;
    using GlobalHookCallback = std::function<bool(ClientObject&, const std::string& hook, const std::any& data)>;
    
    std::mutex trigger_lock;

    std::ofstream debug_log, debug_log_s;

    std::map<uint64_t, GlobalHookCallback> cb_global_hooks;
    std::map<std::string, std::map<uint64_t, HookCallback>> cb_hooks;

    // - - - - - - - -

public:
    ClientObject(asio::io_context& ctx, asio::ip::tcp::socket&& soc, uint64_t id, std::string name):
        ctx(ctx), socket(std::move(soc)), id(id), name(name), consecutiveErrors(0),
        server_authorized(false), authorizing(false), valid(true), in_data(new char[MAX_PAYLOAD_SIZE]),
        in_payload_protection(1), out_payload_protection(1)
    {}

    ~ClientObject();

    ClientObject(const ClientObject&) = delete;
    ClientObject& operator=(const ClientObject&) = delete;

    void server_authorize(); // begin authorize process for server - asynchronous
    void client_authorize(); // begin authorize process for client - asynchronous

    void runtime_update(); // misc blocking update loop

    void send_command(Command&& cmd); // send command to outgoing command queue

    void shutdown(); // a safe way to shutdown the socket
    bool is_valid();
    bool is_authorized();
    size_t get_id() { return id; }
    std::string get_name() { return name; }

    // Hookable system

    uint64_t register_hook(const std::string& hook_name, HookCallback hook);
    uint64_t register_global_hook(GlobalHookCallback hook);
    void unregister_hook(uint64_t id);

    // - - - - - - - - -

private:

    bool send_raw_data(const char* data, size_t length, std::function<void(bool)> on_complete=[](bool){});

    void append_command_package(Command&& cmd); // add command to package buffer
    bool flush_command_package(); // attempt to send command package buffer to socket output buffer - returns false if still flushing previous data

    void reset_and_receive_data(); // Reset incoming handle and start receiving fresh data
    void incoming_command_handle(); // Command length payloads are async-retrieved via this basic retrieve method
    void incoming_data_handle(size_t length); // Command data payloads are async-retrieved via this basic retrieve method

    void process_incoming_commands(); // process all incoming commands synchronously with current thread
    void process_outgoing_commands(); // process outgoing commands synchronously within current thread

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
