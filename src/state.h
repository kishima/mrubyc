#ifndef MRBC_STATE_H_
#define MRBC_STATE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RTcb mrb_tcb;

#ifndef MRBC_ALLOC_FLI_BIT_WIDTH	// 0000 0000 0000 0000
# define MRBC_ALLOC_FLI_BIT_WIDTH 9	// ~~~~~~~~~~~
#endif
#ifndef MRBC_ALLOC_SLI_BIT_WIDTH	// 0000 0000 0000 0000
# define MRBC_ALLOC_SLI_BIT_WIDTH 3	//            ~~~
#endif
#ifndef MRBC_ALLOC_MEMSIZE_T
# define MRBC_ALLOC_MEMSIZE_T     uint16_t
#endif

#define SIZE_FREE_BLOCKS \
  ((MRBC_ALLOC_FLI_BIT_WIDTH + 1) * (1 << MRBC_ALLOC_SLI_BIT_WIDTH))

typedef struct USED_BLOCK {
  unsigned int         t : 1;       //!< FLAG_TAIL_BLOCK or FLAG_NOT_TAIL_BLOCK
  unsigned int         f : 1;       //!< FLAG_FREE_BLOCK or BLOCK_IS_NOT_FREE
  uint8_t              vm_id;       //!< mruby/c VM ID

  MRBC_ALLOC_MEMSIZE_T size;        //!< block size, header included
  MRBC_ALLOC_MEMSIZE_T prev_offset; //!< offset of previous physical block
} USED_BLOCK;

typedef struct FREE_BLOCK {
  unsigned int         t : 1;       //!< FLAG_TAIL_BLOCK or FLAG_NOT_TAIL_BLOCK
  unsigned int         f : 1;       //!< FLAG_FREE_BLOCK or BLOCK_IS_NOT_FREE
  uint8_t              vm_id;       //!< dummy

  MRBC_ALLOC_MEMSIZE_T size;        //!< block size, header included
  MRBC_ALLOC_MEMSIZE_T prev_offset; //!< offset of previous physical block

  struct FREE_BLOCK *next_free;
  struct FREE_BLOCK *prev_free;
} FREE_BLOCK;

typedef struct mrbc_state_t{
	uint8_t id;
	//memory_pool
	unsigned int memory_pool_size;
	uint8_t     *memory_pool;
	FREE_BLOCK *free_blocks[SIZE_FREE_BLOCKS + 1];
	uint16_t free_fli_bitmap;
	uint16_t free_sli_bitmap[MRBC_ALLOC_FLI_BIT_WIDTH + 2]; // + sentinel
	
	//multi-task
	mrb_tcb *q_dormant_;
	mrb_tcb *q_ready_;
	mrb_tcb *q_waiting_;
	mrb_tcb *q_suspended_;
	volatile uint32_t tick_;
	uint8_t ntcbs;
	mrb_tcb** tcbs;

	//class
	/* Class Tree */
	mrb_class *mrbc_class_object;
	/* Proc */
	mrb_class *mrbc_class_proc;
	/* Classes */
	mrb_class *mrbc_class_false;
	mrb_class *mrbc_class_true;
	mrb_class *mrbc_class_nil;
	mrb_class *mrbc_class_array;
	mrb_class *mrbc_class_fixnum;
	mrb_class *mrbc_class_symbol;
	mrb_class *mrbc_class_float;
	mrb_class *mrbc_class_math;
	mrb_class *mrbc_class_string;
	mrb_class *mrbc_class_range;
	mrb_class *mrbc_class_hash;
	
	
}mrbc_state;


#ifdef __cplusplus
}
#endif
#endif 
