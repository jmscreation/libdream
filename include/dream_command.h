#pragma once

#include <string>

namespace dream {


class Command {
public:

    enum Type : uint16_t {
        PING, RESPONSE
    } type;

    Command() = default;
    Command(Type type): type(type) {}


    template<class Archive>
    void serialize(Archive& archive) {
        archive(type);
    }

};






}