#include "buffer.h"

#include "Infiniband.h"

Buffer::Buffer(Infiniband::MemoryManager::Chunk* chunk) : start(reinterpret_cast<void*>(chunk->buffer)), cur(start), len(chunk->bytes) {}