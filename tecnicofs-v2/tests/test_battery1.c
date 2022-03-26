#include "fs/operations.h"
#include <assert.h>
#include <string.h>

pthread_rwlock_t rwlock;

void* testing() {

  char *path1 = "/f1";

  assert(tfs_init() != -1);
    
  int f1 = tfs_open(path1, TFS_O_CREAT);
  assert(f1 != -1);
  assert(tfs_close(f1) != -1);

  assert (tfs_copy_to_external_fs(path1, "./wrong_dir/unexpectedfile") == -1);

  assert(tfs_copy_to_external_fs("/f2", "out") == -1);

  printf("Successful test.\n");

  return NULL;
}

int main() {
  
  pthread_t tid[3];
  assert(tfs_init() != -1);

  if (pthread_rwlock_init(&rwlock, NULL) != 0) return -1;

  pthread_create(&tid[0], NULL, testing, NULL);
  pthread_create(&tid[1], NULL, testing, NULL);
  pthread_create(&tid[2], NULL, testing, NULL);

  pthread_join(tid[0], NULL);
  pthread_join(tid[1], NULL);
  pthread_join(tid[2], NULL);

  tfs_destroy();
  return 0;
}