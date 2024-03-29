#include "dream_socket.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <functional>
#include <algorithm>

namespace dream {

Socket::~Socket() {
    shutdown();
    delete[] in_data;
}

// very low level interface for sending out data asynchronously - this is not to be used externally
bool Socket::send_raw_data(const char* data, size_t length, std::function<void(bool success)> on_complete) {
    std::unique_lock<std::recursive_mutex> lock(shutdown_lock);

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
                dlog << error.message() << "\n";
                shutdown();
                on_complete(false);
            } else {
                on_complete(true);
            }
        }
    );
    return true;
}

// append a new command to the package payload - remember to flush the payload buffer to send the data
void Socket::append_command_package(Command&& cmd) {
    std::stringstream raw;

    auto beg = raw.tellp(); // prepare for overwritten length
    for(uint32_t i=0; i < sizeof(cmdbuf); ++i){
        raw.write("\0", 1); // length reservation
    }

    { 
        cereal::BinaryOutputArchive archive(raw);
        archive(cmd);
    } // enforce stream flush

    raw.seekg(0, std::ios::end);
    if(raw.tellg() > UINT32_MAX) throw std::runtime_error("command payload too large");

    uint32_t plength = uint32_t(raw.tellg()); // get the size of the data stream

    if(plength <= sizeof(plength)){
        dlog << "warning: skipping package due to zero length payload\n";
        return; // something went wrong because there was no payload found
    }
    plength -= sizeof(plength); // decrement the reserved length size in the payload

    raw.seekp(beg); // re-write at beginning
    raw.write(reinterpret_cast<const char*>(&plength), sizeof(plength)); // overwrite the payload length
    raw.seekg(0, std::ios::beg); // read from beginning

    {
        out_payload << raw.rdbuf(); // append to payload cache
    }
}

size_t Socket::check_command_package() {
    return size_t(out_payload.tellp());
}

bool Socket::flush_command_package() {
    if(!out_payload_protection.try_acquire()) return false;

    {
        std::swap(out_payload_flushing, out_payload); // cache buffer swaps for fast performance
        out_payload.str(""); // clear next payload cache for fresh data for next flush
    }

    // This is a C++20 hack that essentially gets a read-only const char* + length buffer view of the stringstream;
    //  It is extremely important that the stringstream object will never change after these pointer values are captured

    const char* data = out_payload_flushing.view().data(); // current payload flushing cache buffer as a raw buffer
    size_t length = out_payload_flushing.view().length(); // calculate the length of the flush buffer

    if(length > 0){ // let's never send nothing
        if(!send_raw_data(data, length, [this](bool success){
            out_payload_protection.release();
        })) out_payload_protection.release(); // whow - release this lock on error
    }

    return true;
}

void Socket::wait_for_flush() {
    do {
        Clock::sleepMilliseconds(2);
        std::unique_lock<std::shared_mutex> lock(outgoing_command_lock);
    } while(!flush_command_package() && is_valid());
}

void Socket::send_command(Command&& cmd) {
    trigger_hook("on_send", cmd);

    std::unique_lock<std::shared_mutex> lock(outgoing_command_lock);
    out_commands.emplace(std::move(cmd)); // move command into the queue
}

void Socket::server_authorize() {
    std::unique_lock<std::recursive_mutex> lock(shutdown_lock);
    if(authorizing) return;

    authorizing = true;

    asio::async_read(socket, asio::buffer(in_data, sizeof(DREAM_PROTO_ACCESS)), [&](const asio::error_code& error, size_t bytes){
        if(error){
            return 0ULL;
        }
        return sizeof(DREAM_PROTO_ACCESS) - bytes;
    }, [&](const asio::error_code& error, size_t bytes){
        if(error || sizeof(DREAM_PROTO_ACCESS) != bytes){
            return; // auth read error - no print or error handling for security
        }
        std::string rcv(in_data, bytes);
        std::string rdx(DREAM_PROTO_ACCESS, sizeof(DREAM_PROTO_ACCESS));
        if( (server_authorized = (rcv == rdx)) ){ // authorized successful
            trigger_hook("on_authorized");
            incoming_command_handle(); // begin incoming data stream
        }
    });

    std::thread timeout([&](){
        dream::Clock::sleepSeconds(3);
        if(!server_authorized){
            dlog << "invalid client - validation timeout\n";
            shutdown();
            valid = false;
        }
        authorizing = false;
    });
    timeout.detach();
}

void Socket::client_authorize() {
    for(int i=0; i < 4 && // retry 3 times
        (
            !out_payload_protection.try_acquire() ||
            !send_raw_data(DREAM_PROTO_ACCESS, sizeof(DREAM_PROTO_ACCESS), [this](bool success){
                if(success){
                    server_authorized = true;
                    trigger_hook("on_authorized"); // for now authorize the client connection immediately after sending the data
                    reset_and_receive_data(); // begin incoming data stream
                }
                out_payload_protection.release();
            })
        ); ++i) Clock::sleepSeconds(1); // 1 second timeout
}

void Socket::runtime_update() {

    process_incoming_commands();

    process_outgoing_commands();
}

void Socket::shutdown() {
    std::unique_lock<std::recursive_mutex> lock(shutdown_lock);

    if(socket.is_open()){
        dlog << "socket " << name << " disconnected\n";
        socket.close();
        trigger_hook("on_disconnected");
    }
}

bool Socket::is_valid() {
    return socket.is_open() && valid;
}

bool Socket::is_authorized() {
    return server_authorized.load();
}

bool Socket::is_authorizing() {
    return authorizing.load();
}

void Socket::reset_and_receive_data() {
    in_payload_protection.release();

    incoming_command_handle();
}

void Socket::incoming_command_handle() {
    if (!in_payload_protection.try_acquire()) {
        dlog << "A serious error has occurred:\nThe incoming data handler was called at an invalid time!\n";
        return;
    }

    in_payload.str(""); // clear the payload buffer
    asio::async_read(socket, asio::buffer(cmdbuf, sizeof(cmdbuf)), [this](const asio::error_code& error, size_t bytes){
        if(error){
            if(!internal_error_check(error)) return 0ULL;
        }
        return sizeof(cmdbuf) - bytes;
    }, [this](const asio::error_code& error, size_t bytes){
        if(error){
            dlog << error.message() << "\n";
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

void Socket::incoming_data_handle(size_t length) {
    size_t overflow = length > MAX_PAYLOAD_SIZE ? length - MAX_PAYLOAD_SIZE : 0;

    asio::async_read(socket, asio::buffer(in_data, length - overflow), [this, length, overflow](const asio::error_code& error, size_t bytes){
        if(error){
            dlog << error.message() << "\n";
            if(!internal_error_check(error)) return 0ULL;
        }
        return length - overflow - bytes;
    }, [this, length, overflow](const asio::error_code& error, size_t bytes) mutable {
        if(error){
            dlog << error.message() << "\n";
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
                        std::unique_lock<std::shared_mutex> lock(incoming_command_lock);
                        in_commands.emplace(std::move(cmd));
                    }
                } catch(cereal::Exception e){
                    dlog << "\tcaught exception: " << e.what() << "\n";
                }
                reset_and_receive_data();
            }
        }
    });
}

bool Socket::internal_error_check(const asio::error_code& error) {
    trigger_hook("internal_error", error);

    if(!socket.is_open() || ++consecutiveErrors > 4){
        shutdown();
        return false; // failed
    }
    return true; // retry
}

void Socket::process_command(Command& cmd) {
    trigger_hook("pre_command", cmd);

    switch(cmd.type){
        case Command::PING:
        {
            std::unique_lock<std::shared_mutex> lock(outgoing_command_lock);
            out_commands.emplace(Command(Command::RESPONSE));
            break;
        }
        case Command::RESPONSE:
        {
            break;
        }
        default:
        {
            break;
        }
    }

    trigger_hook("post_command", cmd);
}


void Socket::process_incoming_commands() {
    std::unique_lock<std::shared_mutex> lock(incoming_command_lock);

    while(!in_commands.empty()){ // process all commands and dequeue
        process_command(in_commands.front());
        in_commands.pop();
    }
}

void Socket::process_outgoing_commands() {
    std::unique_lock<std::shared_mutex> lock(outgoing_command_lock);

    bool flush = false;
    for(; !out_commands.empty(); out_commands.pop()){
        append_command_package(std::move(out_commands.front())); // move all commands into package cache
        if(!flush) flush = true;
    }

    if(flush || check_command_package() > 0){
        flush_command_package(); // flush out the payload stream
    }
}

}
