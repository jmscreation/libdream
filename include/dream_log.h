#pragma once

#include <iostream>
#include <sstream>
#include <fstream>
#include <mutex>

namespace dream {

class Log {
    std::streambuf* _stream;
    std::ostream output;
    std::mutex mtx;

public:
    Log();
    Log(const std::ostream& out);

    template<typename T>
    Log& operator<<(const T& d) {
        std::scoped_lock lock(mtx);
        if(_stream != nullptr) // this is 3x faster
            output << d;
        return *this;
    }

    Log& flush() {
        output.flush();
        return *this;
    }

    Log& redirect(const std::ostream& stream);

};


}