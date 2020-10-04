// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "memory.h"
#include "types/KProcess.h"

namespace skyline::kernel {
    MemoryManager::MemoryManager(const DeviceState &state) : state(state) {}

    void MemoryManager::InitializeRegions(u64 address, u64 size, memory::AddressSpaceType type) {
        switch (type) {
            case memory::AddressSpaceType::AddressSpace32Bit:
                throw exception("32-bit address spaces are not supported");

            case memory::AddressSpaceType::AddressSpace36Bit: {
                addressSpace.address = 0;
                addressSpace.size = 1UL << 36;
                base.address = constant::BaseAddress;
                base.size = 0xFF8000000;
                code.address = base.address;
                code.size = 0x78000000;
                if (code.address > address || (code.size - (address - code.address)) < size)
                    throw exception("Code mapping larger than 36-bit code region");
                alias.address = code.address + code.size;
                alias.size = 0x180000000;
                stack.address = alias.address;
                stack.size = alias.size;
                heap.address = alias.address + alias.size;
                heap.size = 0x180000000;
                tlsIo.address = code.address;
                tlsIo.size = 0;
                break;
            }

            case memory::AddressSpaceType::AddressSpace39Bit: {
                addressSpace.address = 0;
                addressSpace.size = 1UL << 39;
                base.address = constant::BaseAddress;
                base.size = 0x7FF8000000;
                code.address = util::AlignDown(address, 0x200000);
                code.size = util::AlignUp(address + size, 0x200000) - code.address;
                alias.address = code.address + code.size;
                alias.size = 0x1000000000;
                heap.address = alias.address + alias.size;
                heap.size = 0x180000000;
                stack.address = heap.address + heap.size;
                stack.size = 0x80000000;
                tlsIo.address = stack.address + stack.size;
                tlsIo.size = 0x1000000000;
                break;
            }
        }

        chunks = {ChunkDescriptor{
            .ptr = reinterpret_cast<u8*>(base.address),
            .size = base.size,
            .state = memory::states::Unmapped,
        }};

        state.logger->Debug("Region Map:\nCode Region: 0x{:X} - 0x{:X} (Size: 0x{:X})\nAlias Region: 0x{:X} - 0x{:X} (Size: 0x{:X})\nHeap Region: 0x{:X} - 0x{:X} (Size: 0x{:X})\nStack Region: 0x{:X} - 0x{:X} (Size: 0x{:X})\nTLS/IO Region: 0x{:X} - 0x{:X} (Size: 0x{:X})", code.address, code.address + code.size, code.size, alias.address, alias.address + alias.size, alias.size, heap.address, heap
            .address + heap.size, heap.size, stack.address, stack.address + stack.size, stack.size, tlsIo.address, tlsIo.address + tlsIo.size, tlsIo.size);
    }

    void MemoryManager::InsertChunk(const ChunkDescriptor &chunk) {
        std::unique_lock lock(mutex);

        auto upper{std::upper_bound(chunks.begin(), chunks.end(), chunk.ptr, [](const u8 *ptr, const ChunkDescriptor &chunk) -> bool { return ptr < chunk.ptr; })};
        if (upper == chunks.begin())
            throw exception("InsertChunk: Chunk inserted outside address space: 0x{:X} - 0x{:X} and 0x{:X} - 0x{:X}", fmt::ptr(upper->ptr), fmt::ptr(upper->ptr + upper->size), chunk.ptr, fmt::ptr(chunk.ptr + chunk.size));

        upper = chunks.erase(upper, std::upper_bound(upper, chunks.end(), chunk.ptr + chunk.size, [](const u8 *ptr, const ChunkDescriptor &chunk) -> bool { return ptr < chunk.ptr; }));
        if (upper != chunks.end() && upper->ptr < chunk.ptr + chunk.size) {
            auto end{upper->ptr + upper->size};
            upper->ptr = chunk.ptr + chunk.size;
            upper->size = end - upper->ptr;
        }

        auto lower{std::prev(upper)};
        if (lower->ptr == chunk.ptr && lower->size == chunk.size) {
            lower->state = chunk.state;
            lower->permission = chunk.permission;
            lower->attributes = chunk.attributes;
        } else if (chunk.IsCompatible(*lower)) {
            lower->size = lower->size + chunk.size;
        } else {
            if (lower->ptr + lower->size > chunk.ptr)
                lower->size = chunk.ptr - lower->ptr;
            if (upper != chunks.end() && chunk.IsCompatible(*upper)) {
                upper->ptr = chunk.ptr;
                upper->size = chunk.size + upper->size;
            } else {
                chunks.insert(upper, chunk);
            }
        }
    }

    std::optional<ChunkDescriptor> MemoryManager::Get(void* ptr) {
        std::shared_lock lock(mutex);

        auto chunk{std::upper_bound(chunks.begin(), chunks.end(), reinterpret_cast<u8 *>(ptr), [](const u8 *ptr, const ChunkDescriptor &chunk) -> bool { return ptr < chunk.ptr; })};
        if (chunk-- != chunks.begin())
            if ((chunk->ptr + chunk->size) > ptr)
                return std::make_optional(*chunk);

        return std::nullopt;
    }

    size_t MemoryManager::GetProgramSize() {
        size_t size{};
        for (const auto &chunk : chunks)
            size += chunk.size;
        return size;
    }
}
