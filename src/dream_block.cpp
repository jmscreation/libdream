#include "libdream.h"

namespace dream {

Block::Block(): cid(1) {}

Block::~Block() {
    clear();
}

void Block::clear() {
    // free all blobs
    for(auto& [k,v] : blobs) delete v.ptr;
    blobs.clear();
}


}