#include "fs/operations.h"
#include <assert.h>
#include <string.h>

pthread_rwlock_t rwlock;

void* testing() {

  char *path1 = "/f1";
  char *str1 = "OLÁ! OLÁ! OLÁ! ";
  char *path2 = "external_file.txt";
  char to_read[40];

  assert(tfs_init() != -1);
    
  int f1 = tfs_open(path1, TFS_O_CREAT);
  assert(f1 != -1);

  assert(tfs_write(f1, str1, strlen(str1)) != -1);

  assert(tfs_close(f1) != -1);

  assert(tfs_copy_to_external_fs(path1, path2) != -1);

  FILE *fp = fopen(path2, "r");

  assert(fp != NULL);

  assert(fread(to_read, sizeof(char), strlen(str1), fp) == strlen(str1));
    
  assert(strcmp(str1, to_read) == 0);

  assert(fclose(fp) != -1);

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