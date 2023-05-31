
#define BUDDY_LEVELS 10

struct buddy_info{
  uint64 total;
  uint64 free;
  uint64 free_by_size[BUDDY_LEVELS];
};