#define MM_SIZE 8

struct mmapEntry{
    struct file *file;
    void *addr;
};

struct mmapTable{
  struct mmapProc *header;
  int tabNum;
  struct mmapTable *nextTable;
  struct mmapTable *prevTable;
  int free;
  struct mmapEntry *table[MM_SIZE];
};

struct mmapProc{
    int tid;
    struct list_elem elem;
    struct mmapTable *mmapTable;
};


void mmap_init(void);
int assign_mapId(struct file*,void *);
struct mmapEntry* mapId_to_file(int );
void remove_mapId(int);
void close_mapId(int);