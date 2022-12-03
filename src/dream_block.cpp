#include "libdream.h"

namespace dream {

Block::Block(): cid(1) {}

Block::~Block() {
    // free all blobs
    for(auto& [k,v] : blobs) delete v.ptr;
}


}