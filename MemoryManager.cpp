#include "MemoryManager.h"

#include <iostream>

#include <cassert>
#include <cmath>
#include <cstring>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

MemoryManager::MemoryManager(unsigned _wordSize,
                             std::function<int(int, void *)> _allocator)
    : wordSize{_wordSize}, allocator{_allocator}, start{nullptr} {}

MemoryManager::~MemoryManager() { shutdown(); }

void MemoryManager::initialize(std::size_t _sizeInWords) {
  assert(_sizeInWords <= std::numeric_limits<uint16_t>::max());

  shutdown();

  totalSizeBytes = _sizeInWords * wordSize;
  totalSizeWords = _sizeInWords;

  //  start = reinterpret_cast<void *>(mmap(nullptr, totalSizeBytes,
  //                                        PROT_READ | PROT_WRITE,
  //                                        MAP_PRIVATE | MAP_ANONYMOUS, -1,
  //                                        0));

  start = malloc(totalSizeBytes);
  blocks.insert(std::pair<void *, uint16_t>(start, totalSizeWords));
}

void *MemoryManager::allocate(std::size_t _sz) {
  assert(_sz <= totalSizeBytes);
  std::size_t totalWords = std::ceil(static_cast<float>(_sz) / wordSize);

  auto rawList = reinterpret_cast<uint16_t *>(getList());
  int offsetInWords = allocator(totalWords, rawList) * wordSize;
  delete[] rawList;

  if (offsetInWords < 0) {
    return nullptr;
  }

  void *pos = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(start) +
                                       offsetInWords);

  const uint16_t blockSizeWords = blocks.at(pos);
  blocks.erase(pos);

  if (blockSizeWords > totalWords) {
    void *remainingBlockPos = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(pos) + (totalWords * wordSize));
    uint16_t remainingBlockSizeWords = blockSizeWords - totalWords;
    blocks[remainingBlockPos] = remainingBlockSizeWords;
  }

  // Add the allocated block to inUse
  inUse.insert(std::pair<void *, uint16_t>{pos, totalWords});

  return pos;
}

void MemoryManager::free(void *_address) {
  if (!_address)
    return;

  auto fnd_it = inUse.find(_address);
  if (fnd_it != inUse.end()) {
    uint16_t sz = fnd_it->second;
    blocks[_address] = sz; // Add back to blocks
    inUse.erase(fnd_it);   // Remove from inUse
  }

  compact();
}

void MemoryManager::shutdown() {
  if (start) {
    // munmap(start, totalSizeBytes);
    free(start);
    blocks.clear();
    inUse.clear();
    start = nullptr;
  }
}

void *MemoryManager::getMemoryStart() { return start; }

unsigned MemoryManager::getWordSize() { return wordSize; }

void *MemoryManager::getList() {
  std::vector<uint16_t> offsets;
  std::vector<uint16_t> lengths;

  for (auto const &b : blocks) {
    std::size_t offset = reinterpret_cast<uint8_t *>(b.first) -
                         reinterpret_cast<uint8_t *>(start);
    offset /= wordSize;
    uint16_t length = b.second;
    offsets.push_back(offset);
    lengths.push_back(length);
  }

  auto count = static_cast<uint16_t>(blocks.size());

  uint16_t *list =
      new uint16_t[sizeof(uint16_t) + (2 * sizeof(uint16_t) * count)];
  int p = 0;
  list[p] = count;
  p++;

  for (int n = 0; n < count; n++) {
    list[p++] = offsets[n];
    list[p++] = lengths[n];
  }

  return reinterpret_cast<void *>(list);
}

unsigned MemoryManager::getMemoryLimit() { return totalSizeBytes; }

void MemoryManager::setAllocator(std::function<int(int, void *)> _allocator) {
  allocator = _allocator;
}

int MemoryManager::dumpMemoryMap(std::string_view _fileName) {
  // Open the file for writing, creating it if it doesn't exist
  int fd = open(_fileName.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    perror("open");
    return -1;
  }

  auto list = reinterpret_cast<uint16_t *>(getList());
  std::size_t p = 0;
  uint16_t blockCount = list[p++];

  // Write the memory map to the file
  for (size_t i = 0; i < blockCount; i++) {
    uint16_t offset = list[p++];
    uint16_t sizeInWords = list[p++];

    char buf[64];
    int n = snprintf(buf, sizeof(buf), "[%hu, %hu]", offset, sizeInWords);
    if (write(fd, buf, n) == -1) {
      perror("write");
      close(fd);
      return -1;
    }

    // Add separator if it's not the last holes
    if (i < blockCount - 1) {
      if (write(fd, " - ", 3) == -1) {
        perror("write");
        close(fd);
        return -1;
      }
    }
  }

  // Close the file
  if (close(fd) == -1) {
    perror("close");
    return -1;
  }

  return 0;
}

void *MemoryManager::getBitmap() {
  uint16_t bytesNeeded = std::ceil(totalSizeWords / 8.f);

  uint8_t *mapStart = new uint8_t[bytesNeeded + 2];
  mapStart[0] = static_cast<uint8_t>(bytesNeeded & 0x00FF);
  mapStart[1] = static_cast<uint8_t>((bytesNeeded & 0xFF00) >> 8);

  uint8_t *mapPtr = mapStart + 2;
  memset((void *)mapPtr, 0, bytesNeeded);

  const uint16_t *list = reinterpret_cast<uint16_t *>(getList());
  std::size_t p = 0;
  uint16_t blockCount = list[p++];

  for (int i = 0; i < blockCount; i++) {
    uint16_t offset = list[p++];
    uint16_t sizeInWords = list[p++];

    for (uint16_t j = 0; j < sizeInWords; ++j) {
      uint16_t wordIndex = offset + j;
      uint16_t byteIndex = wordIndex / 8;
      uint8_t bitIndex = wordIndex % 8;
      mapPtr[byteIndex] |= (1 << bitIndex);
    }
  }

  for (int i = 0; i < bytesNeeded; i++) {
    mapPtr[i] = ~mapPtr[i];
  }

  uint8_t loneBits = totalSizeWords % 8;
  uint8_t lastByte = bytesNeeded - 1;
  if (loneBits > 0) {
    for (uint16_t bit = loneBits; bit < 8; bit++) {
      mapPtr[lastByte] ^= (1 << bit);
    }
  }

  delete[] list;

  return mapStart;
}

void MemoryManager::printList() {
  int i = 0;
  for (const auto &b : blocks) {
    i++;
    unsigned offset = reinterpret_cast<std::uintptr_t>(b.first) -
                      reinterpret_cast<std::uintptr_t>(start);
    offset /= wordSize;
    std::cout << "Block " << i << " offset: " << offset << "\n";
    std::cout << "Block " << i << " sizeInWords: " << b.second << "\n";
  }
  std::cout << "Total blocks: " << i << "\n\n";
}

void MemoryManager::compact() {
  List compactedBlocks;

  for (const auto& block : blocks) {
    void* currentAddr = block.first;
    uint16_t blockSize = block.second;

    auto nextBlock = blocks.upper_bound(static_cast<void*>(static_cast<uint8_t*>(currentAddr) + blockSize * wordSize));
    if (nextBlock != blocks.end()) {
      if (nextBlock->first == static_cast<void*>(static_cast<uint8_t*>(currentAddr) + blockSize * wordSize)) {
        blockSize += nextBlock->second;
      }
    }

    auto prevBlock = blocks.lower_bound(currentAddr);
    if (prevBlock != blocks.begin()) {
      --prevBlock;
      if (static_cast<void*>(static_cast<uint8_t*>(prevBlock->first) + prevBlock->second * wordSize) == currentAddr) {
        blockSize += prevBlock->second;
        currentAddr = prevBlock->first;
      }
    }

    compactedBlocks[currentAddr] = blockSize;
  }

  blocks = std::move(compactedBlocks);
}

int bestFit(int _sizeInWords, void *_list) {
  const uint16_t *list = reinterpret_cast<uint16_t *>(_list);
  int bestOffset = -1;
  int minDiff = std::numeric_limits<int>::max();
  int p = 0;
  uint16_t count = list[p++];

  for (int i = 0; i < count; i++) {
    uint16_t offset = list[p++];
    uint16_t length = list[p++];

    int diff = length - _sizeInWords;
    if (diff < 0)
      continue;
    if (diff < minDiff) { // not enough space
      bestOffset = offset;
      minDiff = diff;
    }
  }

  return bestOffset;
}

int worstFit(int _sizeInWords, void *_list) {
  const uint16_t *list = reinterpret_cast<uint16_t *>(_list);
  int worstOffset = -1;
  int maxDiff = std::numeric_limits<int>::min();
  int p = 0;
  uint16_t count = list[p++];

  for (int i = 0; i < count; i++) {
    uint16_t offset = list[p++];
    uint16_t length = list[p++];

    int diff = length - _sizeInWords;
    if (diff < 0) // not enough space
      continue;
    if (diff > maxDiff) {
      worstOffset = offset;
      maxDiff = diff;
    }
  }

  return worstOffset;
}
