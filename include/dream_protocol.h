#pragma once

#include <asio.hpp>

#include <cstdlib>
#include <string>
#include <sstream>
#include <iostream>

namespace dream {

namespace proto {

    struct Message {
        struct Header {
            uint32_t id;
            uint32_t length;
        };

        Header header;

        const char* bytes;

        inline Message& operator=(const std::string& data) {
            message = data;
            header.length = message.size();
            bytes = message.c_str();

            return *this;
        }

        inline Message& operator=(Message&& msg) {
            message = std::move(msg.message);
            header.id = msg.header.id;
            header.length = message.size();
            bytes = message.c_str();

            return *this;
        }

        template<typename T>
        Message& operator<<(const T& data) {
            message.assign(reinterpret_cast<const char*>(&data), sizeof(T));
            header.length = message.size();
            bytes = message.c_str();

            return *this;
        }

        template<typename T>
        const Message& operator>>(T& data) const {
            if(sizeof(T) != header.length){
                std::cerr << "incorrect size to extract data into\n";
            } else {
                memcpy(reinterpret_cast<void*>(&data), reinterpret_cast<const void*>(bytes), sizeof(T));
            }

            return *this;
        }

        Message(Message&& msg) { // move constructor emulates move assignment
            *this = std::move(msg);
        }
        Message(const Message& msg) = default; // copy constructor

        Message(): header( Header {UINT32_MAX, 0} ) {}
        Message(uint32_t id) { header.id = id; header.length = 0; }
        Message(const std::string& msg) { *(this) = msg; header.id = 0; }
        Message(uint32_t id, const std::string& msg) { *(this) = msg; header.id = id; }

    private:
        std::string message;
    };


    struct MessageCache {
        // main cache data
        Message msg;
        size_t pos, len;

        // for reading data
        std::array<char, 1024> cache;
        std::string readbuf;

        // current cache status
        enum Status {
            EMPTY, PENDING, PENDING_HEADER, READY
        };
        Status status;

        MessageCache(): msg(""), pos(0), status(EMPTY) {}
    };

}


}