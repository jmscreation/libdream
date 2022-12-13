#pragma once

#include "dream_blobbox.h"
#include "dream_blob.h"

#include <map>
#include <algorithm>
#include <exception>

namespace dream {

class Block {
    uint64_t cid;
    std::map<uint64_t, BlobBox> blobs;
    std::map<std::string, uint64_t> names;

public:
    Block();
    ~Block();

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    void clear(); // completely clear all blob data within this block

    template<typename T, typename... Args>
    Blob<T>& insert_blob(const std::string& name, Args&&... args) {

        blobs.insert( std::make_pair(cid, BlobBox { nullptr, this, false, false }) );
        BlobBox& box = blobs.at(cid);
        Blob<T>* blob = new Blob<T>(&box, cid, std::forward<Args>(args)...);
        box.ptr = blob;
        names.insert_or_assign(name, cid);

        ++cid;

        return *blob;
    }

    template<typename T>
    Blob<T>& get_blob(uint64_t id) {
        if(!blobs.count(id)) throw std::runtime_error("get_blob(id) called with non-existent id");

        return *static_cast<Blob<T>*>(blobs.at(id).ptr);
    }

    template<typename T>
    Blob<T>& get_blob(const std::string& name) {
        if(!names.count(name)) throw std::runtime_error("get_blob(name) called with non-existent name");

        return get_blob<T>(names.at(name));
    }

};




    
}