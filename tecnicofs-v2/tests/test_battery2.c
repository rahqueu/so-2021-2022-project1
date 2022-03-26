#include "fs/operations.h"
#include <assert.h>
#include <string.h>

#define COUNT 4
#define SIZE 5

pthread_rwlock_t rwlock;


void* testing_create_write() {

  char *path = "/f1";
  char input[SIZE]; 
  memset(input, 'A', SIZE);

  char output [SIZE];

  assert(tfs_init() != -1);

  int fd = tfs_open(path, TFS_O_CREAT);
  assert(fd != -1);
  for (int i = 0; i < COUNT; i++) {
    assert(tfs_write(fd, input, SIZE) == SIZE);
  }
  assert(tfs_close(fd) != -1);

  
  fd = tfs_open(path, 0);
  assert(fd != -1 );

  for (int i = 0; i < COUNT; i++) {
    assert(tfs_read(fd, output, SIZE) == SIZE);
    assert (memcmp(input, output, SIZE) == 0);
  }

  assert(tfs_close(fd) != -1);


  printf("Sucessful test\n");

  return NULL;
}




int main() {
  
  pthread_t tid[2];
  assert(tfs_init() != -1);

  if (pthread_rwlock_init(&rwlock, NULL) != 0) return -1;

  pthread_create(&tid[0], NULL, testing_create_write, NULL);
  pthread_create(&tid[1], NULL, testing_create_write, NULL);

  pthread_join(tid[0], NULL);
  pthread_join(tid[1], NULL);

  tfs_destroy();
  return 0;
}