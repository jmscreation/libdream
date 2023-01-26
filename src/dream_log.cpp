#include "dream_log.h"

namespace dream {

Log::Log(): _stream(nullptr), output(_stream) {}

Log::Log(const std::ostream& out): _stream(out.rdbuf()), output(_stream) {}

Log& Log::redirect(const std::ostream& stream) {
    _stream = stream.rdbuf();
    output.rdbuf(_stream);
    return *this;
}

}