#pragma once

#include "ip_tools.h"
#include <string>

namespace dream {

class ClientObject {
    uint64_t id;
    asio::ip::tcp::socket socket;
    std::string name;

public:
    ClientObject(uint64_t id, asio::ip::tcp::socket&& soc, std::string name): id(id), socket(std::move(soc)), name(name) {}

    template<typename Archive>
    void serialize(Archive& ar) {
        ar(id, name);
    }
};


}