#include <stdio.h>
#include <stdlib.h>
#include "stdio.h"
#include "pthread.h"
#include "mrubyc.h"

#define MEMORY_SIZE (1024*30)
static uint8_t memory_pool1[MEMORY_SIZE];
static uint8_t memory_pool2[MEMORY_SIZE];

#define MAX_APPL 10

uint8_t * load_mrb_file(const char *filename)
{
  FILE *fp = fopen(filename, "rb");

  if( fp == NULL ) {
    fprintf(stderr, "File not found (%s)\n", filename);
    return NULL;
  }

  // get filesize
  fseek(fp, 0, SEEK_END);
  size_t size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // allocate memory
  uint8_t *p = malloc(size);
  if( p != NULL ) {
    fread(p, sizeof(uint8_t), size, fp);
  } else {
    fprintf(stderr, "Memory allocate error.\n");
  }
  fclose(fp);

  return p;
}

void thread_1(void *input){
	printf("thread_1\n");
	mrbc_state* mrbc = mrbc_init(memory_pool1, MEMORY_SIZE);
	uint8_t *p[MAX_APPL] = {0};
	p[0] = load_mrb_file( "thread_test1.mrb" );
	if( mrbc_create_task( mrbc, p[0], 0 ) == NULL ){
		printf("1:create task error!\n");
		return;
	}
	printf("thread_1 mrbc_run\n");
	mrbc_run(mrbc);
}

void thread_2(void *input){
	printf("thread_2\n");
	mrbc_state* mrbc = mrbc_init(memory_pool2, MEMORY_SIZE);
	uint8_t *p[MAX_APPL] = {0};
	p[0] = load_mrb_file( "thread_test2.mrb" );
	if( mrbc_create_task(mrbc, p[0], 0 ) == NULL ){
		printf("2:create task error!\n");
		return;
	}
	printf("thread_2 mrbc_run\n");
	mrbc_run(mrbc);
}


int main(int argc, char *argv[])
{
	pthread_t pthread[2];
	void* func[2]={thread_1,thread_2};
	pthread_create( &pthread[0], NULL, func[0], NULL);
	pthread_create( &pthread[1], NULL, func[1], NULL);

	pthread_join(pthread[0], NULL);
	pthread_join(pthread[1], NULL);

	return 0;
}
