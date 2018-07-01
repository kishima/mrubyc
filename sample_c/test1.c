/*
 * Sample Main Program
 */

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "mrubyc.h"

#define MEMORY_SIZE (1024*30)
static uint8_t memory_pool[MEMORY_SIZE];

static int first=0;
static struct VM *vm;

void mrubyc(uint8_t *mrbbuf)
{

	if(!first){
		mrbc_init_alloc(memory_pool, MEMORY_SIZE);
		init_static();
		
		vm = mrbc_vm_open(NULL);
		if( vm == 0 ) {
			fprintf(stderr, "Error: Can't open VM.\n");
			return;
		}
	}
	
	if( mrbc_load_mrb(vm, mrbbuf) != 0 ) {
		fprintf(stderr, "Error: Illegal bytecode.\n");
		return;
	}
	
	if(!first){
		mrbc_vm_begin(vm);
	}else{
		vm->pc_irep = vm->irep;
		vm->pc = 0;
		vm->current_regs = vm->regs;
		vm->flag_preemption = 0;
		vm->callinfo_top = 0;
		vm->error_code = 0;
		printf("vm->regs[0].tt=%d\n",vm->regs[0].tt);
	}
	printf("RUN!\n");
	mrbc_vm_run(vm);
	printf("DONE!\n");
	//mrbc_vm_end(vm);
	//mrbc_vm_close(vm);
	if(first==0)first=1;
}

unsigned char buff[10000];
int buff_ptr=0;

int main(int argc, char *argv[])
{
	//if( argc != 2 ) {
	//	printf("Usage: %s <xxxx.mrb>\n", argv[0]);
	//	return 1;
	//}
	printf("rmirb: connect the M5Stack\n");
	
	const char* server_ip = "127.0.0.1";
	int port = 33333;
	printf("rmirb: IP:%s Port:%d\n",server_ip,port);

	struct sockaddr_in server;
	char buf[32];
	int n;
	
	int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_socket<0){
		printf("socket error: %s\n",strerror(errno));
		return 0;
	}
	
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = inet_addr(server_ip);

	bind(listen_socket, (struct sockaddr *)&server, sizeof(server));
	listen(listen_socket,1);

	while(1){
		struct sockaddr_in client;
		int len;
		printf("accept start\n");
		int sock = accept(listen_socket, (struct sockaddr *)&client, &len);
		if(sock<0){
			printf("accept error: %s\n",strerror(errno));
			continue;
		}
		int remain=4;
		unsigned char header[4];
		while(1){
			printf("recv start remain=%d\n",remain);
			int size = recv(sock,&header[4-remain],remain,0);
			printf("recv size = %d\n",size);
			if(size<0){
				printf("recv error: %s\n",strerror(errno));
				close(sock);
				break;
			}
			if(size<remain){
				remain-=size;
				continue;
			}
			
			uint16_t irep_len = bin_to_uint16(&header[2]);
			printf("type=%d, irep size=%d recv start\n",header[1],irep_len);
			remain = irep_len;
			while(1){
				size = recv(sock,&buff[buff_ptr+(irep_len-remain)],remain,0);
				if(size<0){
					printf("recv error: %s\n",strerror(errno));
					close(sock);
					break;
				}
				if(size<remain){
					remain-=size;
					continue;
				}
				break;
			}
			int i=0;
			printf("size=%d\n",size);
			for(i=0;i<size;i++){
				printf("0x%02X ",buff[buff_ptr+i]);
			}
			printf("\n");
			printf(">>>>>>>>>>>>>>>>>>>>> Run VM\n");
			mrubyc(&buff[buff_ptr]);
			buff_ptr+=irep_len;
			remain=4;
		}
	}	
	return 0;
}
