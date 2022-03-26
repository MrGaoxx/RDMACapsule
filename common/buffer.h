
#ifndef COMMON_BUFFER_H
#define COMMON_BUFFER_H

#include <cstdint>
#include <iostream>
#include <list>
//#include <vector>
#include "common/common.h"

class Buffer {
   public:
    explicit Buffer() : start(nullptr), cur(nullptr), len(0){};
    explicit Buffer(uint32_t size) : start(reinterpret_cast<char*>(malloc(sizeof(size)))), cur(start), len(size), is_malloc(true) {}
    explicit Buffer(char* buffer_start, uint32_t buffer_len) : start(buffer_start), cur(start), len(buffer_len) {}
    explicit Buffer(const Buffer& buffer) : start(buffer.start), cur(buffer.cur), len(buffer.len) {}
    Buffer(Buffer& buffer) : Buffer(static_cast<const Buffer&>(buffer)) {}
    // explicit Buffer(const Buffer&& buffer) : start(buffer.start), cur(buffer.cur), len(buffer.len) {}
    ~Buffer() {
        if (is_malloc) {
            free(start);
        }
    }
    Buffer& operator=(const Buffer& buffer) {
        start = buffer.start;
        cur = buffer.cur;
        len = buffer.len;
    }
    uint32_t get_len() { return len; }
    uint32_t GetRemainingLen() const { return static_cast<uint32_t>(len - (cur - start)); }
    char* get_buffer() { return cur; }
    char* get_raw_buffer() { return start; }
    uint32_t Move(uint32_t size) {
        char* prev = cur;
        cur = ((start + len) > (cur + size)) ? cur + size : start + len;
        return (cur < start + len) ? 0 : (size - static_cast<uint32_t>(cur - prev));
    }

   private:
    char* start;
    char* cur;
    uint32_t len;
    bool is_malloc = false;
};

class BufferList {
   public:
    explicit BufferList() : buffer_list(), len(0){};
    explicit BufferList(Buffer& buffer) : buffer_list{buffer}, len(buffer.GetRemainingLen()){};

    BufferList& Append(const Buffer& buffer) {
        buffer_list.push_back(buffer);
        len += buffer.GetRemainingLen();
        return *this;
    }

    BufferList& Append(BufferList& bufferList) {
        for (auto& buffer : bufferList.GetBufferList()) {
            buffer_list.push_back(buffer);
            len += buffer.GetRemainingLen();
        }
        return *this;
    }

    // std::list<Buffer>& GetBufferList() { return buffer_list; }

    uint32_t Move(uint32_t size) {
        while (size && !buffer_list.empty()) {
            uint32_t pre_size = size;
            size = buffer_list.front().Move(size);
            kassert(len > size - pre_size);
            len -= size - pre_size;
            if (size) {
                kassert(0 == buffer_list.front().GetRemainingLen());
                buffer_list.pop_front();
            }
        }
    };

    uint32_t GetSize() { return buffer_list.size(); }
    uint32_t get_len() { return len; }
    Buffer& GetNBuffer(uint8_t n) {
        auto iter = buffer_list.begin();
        for (int i = 0; i < n % buffer_list.size(); i++) iter++;
        return *iter;
    }
    BufferList& DeleteNBuffer(uint8_t n) {
        auto iter = buffer_list.begin();
        for (int i = 0; i < n % buffer_list.size(); i++) iter++;
        buffer_list.erase(iter);
        return *this;
    }

    using BufferIterator = std::list<Buffer>::iterator;
    BufferIterator&& get_begin() {
        auto rval = buffer_list.begin();
        return std::forward<BufferIterator>(rval);
    }
    BufferIterator&& get_end() {
        auto rval = buffer_list.end();
        return std::forward<BufferIterator>(rval);
    }
    void Clear() { buffer_list.clear(); }
    std::list<Buffer>& GetBufferList() { return buffer_list; }

   private:
    std::list<Buffer> buffer_list;
    uint32_t len;
};

#endif