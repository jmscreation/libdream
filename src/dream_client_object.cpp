#include "dream_client_object.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <functional>
#include <algorithm>

namespace dream {

ClientObject::~ClientObject() {
    shutdown();
    delete[] in_data;
}

// very low level interface for sending out data asynchronously - this is not to be used externally
bool ClientObject::send_raw_data(const char* data, size_t length, std::function<void(bool success)> on_complete) {
    if(!is_valid()) return false;

    auto buf = asio::buffer(data, length);
    
    asio::async_write(socket, buf,
        [this, length](const asio::error_code& error, size_t bytes){
            if(error){
                if(!internal_error_check(error)) return 0ULL;
            }
            return length - bytes;
        },
        [this, on_complete](const asio::error_code& error, size_t bytes){
            if(error){
                std::cout << error.message() << "\n";
                shutdown();
                on_complete(false);
            } else {
                on_complete(true);
            }
        }
    );
    return true;
}

void ClientObject::append_command_package(Command&& cmd) {
    std::scoped_lock lock(outgoing_command_lock);

    std::stringstream raw;

    auto beg = raw.tellp(); // prepare for overwritten length
    for(int i=0; i < sizeof(cmdbuf); ++i){
        raw.write("\0", 1); // length reservation
    }

    { 
        cereal::BinaryOutputArchive archive(raw);
        archive(cmd);
    } // enforce stream flush

    raw.seekg(0, std::ios::end);
    uint32_t plength = raw.tellg(); // get the size of the data stream

    if(plength <= sizeof(plength)){
        std::cout << "warning: skipping package due to zero length payload\n";
        return; // something went wrong because there was no payload found
    }
    plength -= sizeof(plength); // decrement the reserved length size in the payload

    raw.seekp(beg); // re-write at beginning
    raw.write(reinterpret_cast<const char*>(&plength), sizeof(plength)); // overwrite the payload length
    raw.seekg(0, std::ios::beg); // read from beginning

    out_payload << raw.rdbuf(); // append to payload cache

    if(flush_command_package()){
        out_payload.str(""); // clear command payload cache for fresh data for next flush (new data will not make this flush)
    }
}

bool ClientObject::flush_command_package() {
    if(!out_payload_protection.try_acquire()) return false;

    std::swap(out_data, out_payload); // cache buffer swaps for fast performance

    // This is a C++20 hack that essentially gets a read-only const char* + length buffer view of the stringstream;
    //  It is extremely important that the stringstream object will never change after these pointer values are captured
    send_raw_data(out_data.view().data(), out_data.view().length(), [this](bool success){
        if(!success) std::cout << "there was an internal send failure\n";
        out_payload_protection.release();
    });

    return true;
}

void ClientObject::send_command(Command&& cmd) {
    std::scoped_lock lock(runtime_command_lock);

    out_commands.emplace(std::move(cmd)); // move command into the queue
}

void ClientObject::server_authorize() {
    if(authorizing) return;
    authorizing = true;

    std::thread timeout([&](){
        Clock::sleepSeconds(3);
        if(!server_authorized){
            std::cout << "invalid client - validation timeout\n";
            shutdown();
            valid = false;
        }
    });
    timeout.detach();

    asio::async_read(socket, asio::buffer(in_data, sizeof(DREAM_PROTO_ACCESS)), [&](const asio::error_code& error, size_t bytes){
        if(error){
            // internal_error_check(error);
            return 0ULL;
        }
        return sizeof(DREAM_PROTO_ACCESS) - bytes;
    }, [&](const asio::error_code& error, size_t bytes){
        if(error || sizeof(DREAM_PROTO_ACCESS) != bytes){
            return; // auth read error - no print or error handling for security
        }
        std::string rcv(in_data, bytes);
        std::string rdx(DREAM_PROTO_ACCESS, sizeof(DREAM_PROTO_ACCESS));
        if(server_authorized = (rcv == rdx)){ // authorized successful
            authorizing = false;
            trigger_hook("on_authorized");
            incoming_command_handle(); // begin incoming data stream
            // debug_log.open("client_" + std::to_string(id) + ".log", std::ios::out);
        }
    });
}

void ClientObject::client_authorize() {
    for(int i=0; i < 4 && // retry 3 times
        !send_raw_data(DREAM_PROTO_ACCESS, sizeof(DREAM_PROTO_ACCESS), [&](bool success){
            if(success){
                server_authorized = true;
                trigger_hook("on_authorized"); // for now authorize the client connection immediately after sending the data
                reset_and_receive_data(); // begin incoming data stream
                // debug_log_s.open("client.log", std::ios::out);
            }
        }); ++i) Clock::sleepSeconds(1); // 1 second timeout
}

void ClientObject::runtime_update() {
    std::scoped_lock lock(runtime_command_lock);

    process_incoming_commands();

    process_outgoing_commands();
}

void ClientObject::shutdown() {
    std::scoped_lock lock(shutdown_lock);

    if(socket.is_open()){
        std::cout << name << " connection closed\n";
        socket.close();
        trigger_hook("on_disconnected");
    }
}

bool ClientObject::is_valid() {
    return socket.is_open() && valid;
}

bool ClientObject::is_authorized() {
    return server_authorized;
}

void ClientObject::reset_and_receive_data() {
    in_payload_protection.release();

    incoming_command_handle();
}

void ClientObject::incoming_command_handle() {
    if (!in_payload_protection.try_acquire()) {
        std::cout << "A serious warning is issued:\nThe incoming data handler was called at an invalid time!\n";
        return;
    }

    in_payload.str(""); // clear the payload buffer
    asio::async_read(socket, asio::buffer(cmdbuf, sizeof(cmdbuf)), [this](const asio::error_code& error, size_t bytes){
        if(error){
            debug_log << "\terror: failed to load some bytes: " << error.message() << "\n";
            if(!internal_error_check(error)) return 0ULL;
        }
        return sizeof(cmdbuf) - bytes;
    }, [this](const asio::error_code& error, size_t bytes){
        if(error){
            std::cout << error.message() << "\n";
            if(internal_error_check(error)) reset_and_receive_data();
        } else {

            uint32_t len = *std::launder(reinterpret_cast<uint32_t*>(cmdbuf));
            if(!len){
                reset_and_receive_data();
            } else {
                incoming_data_handle(len); // 4 byte payload length sent to data payload retriever
            }
        }
    });
}

void ClientObject::incoming_data_handle(size_t length) {
    size_t overflow = length > MAX_PAYLOAD_SIZE ? length - MAX_PAYLOAD_SIZE : 0;

    debug_log << "expect " << (length - overflow) << " bytes...\n";
    asio::async_read(socket, asio::buffer(in_data, length - overflow), [this, length, overflow](const asio::error_code& error, size_t bytes){
        if(error){
            std::cout << error.message() << "\n";
            if(!internal_error_check(error)) return 0ULL;
        }
        return length - overflow - bytes;
    }, [this, length, overflow](const asio::error_code& error, size_t bytes) mutable {
        if(error){
            debug_log << "\tError: async_read() failed - " << error.message() << "\n";
            std::cout << error.message() << "\n";
            if(internal_error_check(error)) reset_and_receive_data();
        } else {
            in_payload.write(in_data, bytes);

            length -= length - overflow; // decrease overall payload length
            if(length > 0){ // more data that needs to be read
                incoming_data_handle(length); // continue reading data with the left-over payload size
            } else {
                Command cmd;
                try {
                    cereal::BinaryInputArchive fetch(in_payload);
                    fetch(cmd); // process data back into command
                    {
                        std::scoped_lock lock(runtime_command_lock);
                        in_commands.emplace(std::move(cmd));
                    }
                } catch(cereal::Exception e){
                    debug_log << "\tcaught exception: " << e.what() << "\n";
                    std::cout << "\tcaught exception: " << e.what() << "\n";
                }
                reset_and_receive_data();
            }
        }
    });
}

bool ClientObject::internal_error_check(const asio::error_code& error) {
    trigger_hook("internal_error", error);

    if(!socket.is_open() || ++consecutiveErrors > 4){
        shutdown();
        return false; // failed
    }
    return true; // retry
}

void ClientObject::process_command(Command& cmd) {
    trigger_hook("pre_command", cmd);

    switch(cmd.type){
        case Command::PING:
        {
            out_commands.emplace(Command(Command::RESPONSE));
        }
        break;
    }

    trigger_hook("post_command", cmd);
}


void ClientObject::process_incoming_commands() {
    while(!in_commands.empty()){ // process all commands and dequeue
        process_command(in_commands.front());
        in_commands.pop();
    }
}

void ClientObject::process_outgoing_commands() {
    for(; !out_commands.empty(); out_commands.pop()){
        append_command_package(std::move(out_commands.front())); // move all commands into package cache
    }
}

// Hookable Implementations

std::atomic<uint64_t> ClientObject::_hook_id_counter = 0;

uint64_t ClientObject::register_hook(const std::string& hook_name, HookCallback hook) {
    std::scoped_lock lock(trigger_lock);
    uint64_t id = _hook_id_counter++;

    if(!cb_hooks.count(hook_name)){
        cb_hooks.insert_or_assign(hook_name, std::map<uint64_t, HookCallback> {}); // insert fresh empty map
    }

    auto& hooks = cb_hooks.at(hook_name);

    hooks.insert_or_assign(id, hook);

    return id;
}

uint64_t ClientObject::register_global_hook(GlobalHookCallback hook) {
    std::scoped_lock lock(trigger_lock);
    uint64_t id = _hook_id_counter++;

    cb_global_hooks.insert_or_assign(id, hook);

    return id;
}

void ClientObject::unregister_hook(uint64_t id) {
    std::scoped_lock lock(trigger_lock);

    if(cb_global_hooks.count(id)){ // check the global hooks for a trigger that contains the hook id
        cb_global_hooks.erase(id);
        return;
    }

    for(auto& [_, hooks] : cb_hooks){
        if(hooks.count(id)){ // find the hook trigger that contains the hook id
            hooks.erase(id);
            return;
        }
    }    
}

void ClientObject::trigger_hook(const std::string& hook_name, const std::any& data) {
    std::scoped_lock lock(trigger_lock);

    for(auto& [id, cb] : cb_global_hooks){
        if(!cb(*this, hook_name, data))
            return; // check for global hook override - false return will abort remaining hook triggers
    }

    if(!cb_hooks.count(hook_name)) return;

    auto& hook_list = cb_hooks.at(hook_name);
    for(auto& [id, cb] : hook_list){
        cb(*this, data);
    }
}


}
