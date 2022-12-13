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
        [length](const asio::error_code& error, size_t bytes){
            if(error){
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
    auto* raddr = &raw;
    { 
        cereal::BinaryOutputArchive archive(raw);
        archive(cmd);
    } // enforce stream flush
    
    // This is a C++20 hack that essentially gets a read-only const char* + length buffer view of the stringstream;
    //  It is extremely important that the stringstream object will never change after these pointer values are captured
    send_raw_data(raw.view().data(), raw.view().length(), [this, cmd, raddr](bool success){
        std::scoped_lock lock(outgoing_command_lock);
        std::erase_if(out_data.begin(), out_data.end(), [&](const auto& o){ return &o = raddr; });

        std::cout << "sent command type " << cmd.type << " to " << name << " " << (success ? "success" : "failed") << "\n";
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
            return 0ULL;
        }
        return sizeof(authbuf) - bytes;
    }, [&](const asio::error_code& error, size_t bytes){
        if(error) return;
        std::string rcv(authbuf, 5);
        if(server_authorized = (rcv == "GET /")){
            authorizing = false;
        }
    });
}

void ClientObject::client_authorize() {
    memcpy(authbuf, "GET /", 5); // temporary authorization code -- this code lets a browser client connect
    send_raw_data(authbuf, sizeof(authbuf));
}

void ClientObject::runtime_update() {
}

void ClientObject::shutdown() {
    std::scoped_lock lock(shutdown_lock);
    if(socket.is_open()){
        std::cout << name << " connection closed\n";
        socket.close();
    }
}

bool ClientObject::is_valid() {
    return socket.is_open() && valid;
}

bool ClientObject::is_authorized() {
    return server_authorized;
}


bool ClientObject::incoming_data_handle() {
    return true;
}

bool ClientObject::outgoing_data_handle() {
    return true;
}

void ClientObject::process_command(Command& cmd) {
    switch(cmd.type){
        case Command::PING:
        break;
    }
}


}
