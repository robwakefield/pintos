#define FD_SIZE 8

struct fdTable{
  int tid;
  int tabNum;
  struct list_elem elem;
  struct fdTable *nextTable;
  struct fdTable *prevTable;
  int free;
  struct file * table[FD_SIZE];
};

void file_init(void);
struct file* fd_to_file(int);
int assign_fd (struct file *);
void remove_fd (int);
void close_files (int);