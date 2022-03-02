
#include <stdint.h>

#include <iostream>
#include <list>
//#include <vector>

class Buffer {
   public:
    explicit Buffer() : start(nullptr), len(0){};
    explicit Buffer(uint32_t size) : start(malloc(sizeof(size))), len(size){};
    explicit Buffer(const Buffer& buffer) : start(buffer.start), len(buffer.len){};
    explicit Buffer(const Buffer&& buffer) : start(buffer.start), len(buffer.len){};
    Buffer& operator=(const Buffer& buffer) {
        start = buffer.start;
        len = buffer.len;
    }
    void* get_buffer() { return start; }
    void* start;

   private:
    uint32_t len;
};

class BufferList {
   public:
    explicit BufferList(Buffer& buffer) : buffer_list{buffer} {};
    BufferList& Append(const Buffer& buffer) { buffer_list.push_back(buffer); }
    std::list<Buffer>& GetBufferList() { return buffer_list; }
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

   private:
    std::list<Buffer> buffer_list;
};