#include "dream_client_object.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <functional>
#include <algorithm>

namespace dream {

ClientObject::~ClientObject() {
    shutdown();
}


bool ClientObject::send_raw_data(const char* data, size_t length, std::function<void(bool success)> on_complete) {
    if(!is_valid()) return false;

    auto buf = asio::buffer(data, length);
    
    asio::async_write(socket, buf,
        [&, length](const asio::error_code& error, size_t bytes){
            if(error){
                if(++consecutiveErrors > 16) shutdown();
                return 0ULL;
            }
            return length - bytes;
        },
        [&, on_complete](const asio::error_code& error, size_t bytes){
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

void ClientObject::send_command(const Command& cmd) {
    std::scoped_lock lock(outgoing_command_lock);
    std::stringstream& raw = out_data.emplace_back();

    auto beg = raw.tellp(); // prepare for overwritten length
    raw.write("\0\0\0\0", 4); // length reservation

    auto* raddr = &raw;
    { 
        cereal::BinaryOutputArchive archive(raw);
        archive(cmd);
    } // enforce stream flush

    raw.seekg(0, std::ios::end);
    uint32_t plength = raw.tellp(); // get the size of the data stream

    if(plength <= 4) return; // something went wrong because there was no payload found
    plength -= 4; // decrement the reserved length size in the payload

    raw.seekp(beg); // re-write at beginning
    raw.write(reinterpret_cast<const char*>(&plength), 4); // overwrite the payload length
    raw.seekg(0, std::ios::beg); // read from beginning

    // This is a C++20 hack that essentially gets a read-only const char* + length buffer view of the stringstream;
    //  It is extremely important that the stringstream object will never change after these pointer values are captured
    send_raw_data(raw.view().data(), raw.view().length(), [this, raddr](bool success){
        std::scoped_lock lock(outgoing_command_lock);
        std::remove_if(out_data.begin(), out_data.end(), [&](const auto& o) -> bool { return &o == raddr; });
    });
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

    asio::async_read(socket, asio::buffer(authbuf, sizeof(authbuf)), [&](const asio::error_code& error, size_t bytes){
        if(error){
            internal_error_check(error);
            return 0ULL;
        }
        return sizeof(authbuf) - bytes;
    }, [&](const asio::error_code& error, size_t bytes){
        if(error) return; // auth read error - no print or error handling for security
        std::string rcv(authbuf, 5);
        if(server_authorized = (rcv == "GET /")){ // authorized successful
            authorizing = false;
            trigger_hook("on_authorized");
            incoming_command_handle(); // begin incoming data stream
        }
    });
}

void ClientObject::client_authorize() {
    memcpy(authbuf, "GET /", 5); // temporary authorization code -- this code lets a browser client connect
    send_raw_data(authbuf, sizeof(authbuf), [&](bool success){
        if(success){
            server_authorized = true;
            trigger_hook("on_authorized"); // for now authorize the client connection immediately after sending the data
            incoming_command_handle(); // begin incoming data stream
        }
    });
}

void ClientObject::runtime_update() {
    std::scoped_lock lock(runtime_command_lock);
    while(!in_commands.empty()){ // process all commands and dequeue
        process_command(in_commands.front());
        in_commands.pop();
    }
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

void ClientObject::incoming_command_handle() {

    asio::async_read(socket, asio::buffer(cmdbuf, sizeof(cmdbuf)), [&](const asio::error_code& error, size_t bytes){
        if(error){
            std::cout << error.message() << "\n";
            internal_error_check(error);
            return 0ULL;
        }
        return sizeof(cmdbuf) - bytes;
    }, [&](const asio::error_code& error, size_t bytes){
        if(error){
            std::cout << error.message() << "\n";
            if(internal_error_check(error)) incoming_command_handle();
        } else {
            in_payload.str(""); // clear the payload buffer
            incoming_data_handle(*reinterpret_cast<uint32_t*>(cmdbuf)); // 4 byte payload length sent to data payload retriever
        }
    });
}

void ClientObject::incoming_data_handle(size_t length) {
    size_t overflow = length > MAX_PAYLOAD_SIZE ? length - MAX_PAYLOAD_SIZE : 0;

    asio::async_read(socket, asio::buffer(in_data, length - overflow), [&, length, overflow](const asio::error_code& error, size_t bytes){
        if(error){
            std::cout << error.message() << "\n";
            internal_error_check(error);
            return 0ULL;
        }
        return length - overflow - bytes;
    }, [&, length, overflow](const asio::error_code& error, size_t bytes) mutable {
        if(error){
            std::cout << error.message() << "\n";
            if(internal_error_check(error)) incoming_command_handle();
        } else {
            in_payload.write(in_data, bytes);

            length -= length - overflow; // decrease overall payload length
            if(length > 0){ // more data that needs to be read
                incoming_data_handle(length); // continue reading data with the left-over payload size
            } else {
                {
                    std::scoped_lock lock(runtime_command_lock);
                    Command& cmd = in_commands.emplace();
                    cereal::BinaryInputArchive fetch(in_payload);
                    fetch(cmd); // process data back into command
                }
                incoming_command_handle();
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
            std::cout << "ping request\n";
        }
        break;

    }

    trigger_hook("post_command", cmd);
}

// Hookable Implementations

std::atomic<uint64_t> ClientObject::_hook_id_counter = 0;

uint64_t ClientObject::register_hook(const std::string& hook_name, HookCallback hook) {
    uint64_t id = _hook_id_counter++;

    if(!cb_hooks.count(hook_name)){
        cb_hooks.insert_or_assign(hook_name, std::map<uint64_t, HookCallback> {}); // insert fresh empty map
    }

    auto& hooks = cb_hooks.at(hook_name);

    hooks.insert_or_assign(id, hook);

    return id;
}

void ClientObject::unregister_hook(uint64_t id) {
    for(auto& [_, hooks] : cb_hooks){
        if(hooks.count(id)){ // find the hook trigger that contains the hook id
            hooks.erase(id);
            return;
        }
    }
}

void ClientObject::trigger_hook(const std::string& hook_name, const std::any& data) {
    if(!cb_hooks.count(hook_name)) return;

    auto& hook_list = cb_hooks.at(hook_name);
    for(auto& [id, cb] : hook_list){
        cb(*this, data);
    }
}


}
