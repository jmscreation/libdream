#pragma once

/*
    Dream Blob is a blob of data that can be serialized + synchronized
*/

#include "dream_blobbox.h"

#include <variant>
#include <optional>
#include <memory>
#include <string>

namespace dream {

template<typename T>
class Blob : public BasicBlob {
    BlobBox* blob_box;
    std::optional<uint64_t> id; // id or orhpan
    T* data;

    void set_dirty(bool dirty=true) {
        if(!blob_box) return;
        blob_box->dirty = dirty;
    }

public:
    Blob(): blob_box(nullptr), id({}), data(nullptr) {}

    template<typename... Args>
    Blob(BlobBox* box, uint64_t id, Args&&... args): blob_box(box), id(id), data(new T(std::forward<Args>(args)...)) {} // block populate method

    ~Blob() {
        delete data;
        data = nullptr;
    }

    T* operator->() { // direct access to object for reading / writing // this will make the blob dirty
        set_dirty();
        return data;
    }

    T& operator*() { // return a reference access to object for reading / writing // this will make the blob dirty
        set_dirty();
        return *data;
    }

    Block* get_owner() {
        if(!blob_box) return nullptr;
        return blob_box->owner;
    }

    const T& get() const { // return a const reference access to object
        return *static_cast<const T*>(data);
    }

    bool valid() const {
        return data != nullptr;
    }

    template<class Archive>
    void serialize(Archive& archive) {
        archive(id.value(), *data);
    }

};




}