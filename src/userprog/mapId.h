#define MM_SIZE 8



struct mmapTable{
  struct mmapProc *header;
  int tabNum;
  struct mmapTable *nextTable;
  struct mmapTable *prevTable;
  int free;
  void* table[MM_SIZE];
};

struct mmapProc{
    int tid;
    struct list_elem elem;
    struct mmapTable *mmapTable;
};


void mmap_init(void);
int assign_mapId(void *);
void* mapId_to_file(int );
void remove_mapId(int);
void close_mapId(int);