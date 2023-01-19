#pragma once

#include <string>
#include "lib_cereal.h"
#include "dream_blob.h"

namespace dream {


class Command {
public:

    enum Type : uint16_t {
        NILL, PING, RESPONSE, TEST, INHERITED
    } type;

    std::string data;

    virtual ~Command() = default; // inherit for user custom Command types

    Command() = default;
    Command(Type type): type(type) {}
    Command(Type type, const std::string& string): type(type), data(string) { }
    Command(Type type, const char* raw, size_t length): type(type), data(raw, length) {}
    
    template<typename T>
    Command(Type type, const Blob<T>& blob): type(type) {
        std::stringstream output;
        {
            cereal::BinaryOutputArchive archive(output);
            archive(blob);
        } // enforce flush
        data = output.str();
    }


    template<class Archive>
    void serialize(Archive& archive) {
        archive(type, data);
    }

};






}