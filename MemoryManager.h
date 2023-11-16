#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <vector>
#include <map>

class MemoryManager {
public:
  MemoryManager(unsigned _wordSize, std::function<int(int, void *)> _allocator);
  ~MemoryManager();

  void initialize(std::size_t _sizeInWords);
  void shutdown();
  void *allocate(std::size_t _sz);
  void free(void *_address);

  void setAllocator(std::function<int(int, void *)> _allocator);

  int dumpMemoryMap(std::string_view _filename);

  void *getList();

  void *getBitmap();

  unsigned getWordSize();

  void *getMemoryStart();

  unsigned getMemoryLimit();

private:
  std::size_t wordSize;
  uint16_t totalSizeWords;
  std::size_t totalSizeBytes;

  std::function<int(int, void *)> policy;

  void *start;

  using List = std::map<void*, uint16_t>;

  List inUse;
  List blocks;

  void compact();
};

int bestFit(int sizeInWords, void *list);
int worstFit(int sizeInWords, void *list);
