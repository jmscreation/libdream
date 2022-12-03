#pragma once

namespace dream {

// forward declarator
class Block;

class BasicBlob {}; // common inheritance for all template types

struct BlobBox {
    BasicBlob* ptr;
    Block* owner;
    bool dirty, read_only;
};

}