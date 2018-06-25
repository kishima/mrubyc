/*! @file
  @brief
  mruby bytecode executor.

  <pre>
  Copyright (C) 2015-2018 Kyushu Institute of Technology.
  Copyright (C) 2015-2018 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  Fetch mruby VM bytecodes, decode and execute.

  </pre>
*/

#include "vm_config.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "vm.h"
#include "alloc.h"
#include "static.h"
#include "opcode.h"
#include "class.h"
#include "symbol.h"
#include "console.h"

#include "c_string.h"
#include "c_range.h"
#include "c_array.h"
#include "c_hash.h"


static uint32_t free_vm_bitmap[MAX_VM_COUNT / 32 + 1];
#define FREE_BITMAP_WIDTH 32
#define Num(n) (sizeof(n)/sizeof((n)[0]))

//#define RDEBUG

static char* ttname[]={
	"MRB_TT_HANDLE",
	"MRB_TT_EMPTY",
	"MRB_TT_NIL",
	"MRB_TT_FALSE",		// (note) true/false threshold. see op_jmpif
	"MRB_TT_TRUE",
	"MRB_TT_FIXNUM",
	"MRB_TT_FLOAT",
	"MRB_TT_SYMBOL",
	"MRB_TT_CLASS",
	"MRB_TT_OBJECT",
	"MRB_TT_PROC",
	"MRB_TT_ARRAY",
	"MRB_TT_STRING",
	"MRB_TT_RANGE",
	"MRB_TT_HASH",
	"UNKNOWN",
};
char* get_tt_name(mrb_vtype tt){
	switch(tt){
	  case MRB_TT_HANDLE: return ttname[0]; break;
	  case MRB_TT_EMPTY: return ttname[1]; break;
	  case MRB_TT_NIL: return ttname[2]; break;
	  case MRB_TT_FALSE: return ttname[3]; break;
	  case MRB_TT_TRUE: return ttname[4]; break;
	  case MRB_TT_FIXNUM: return ttname[5]; break;
	  case MRB_TT_FLOAT: return ttname[6]; break;
	  case MRB_TT_SYMBOL: return ttname[7]; break;
	  case MRB_TT_CLASS: return ttname[8]; break;
	  case MRB_TT_OBJECT: return ttname[9]; break;
	  case MRB_TT_PROC: return ttname[10]; break;
	  case MRB_TT_ARRAY: return ttname[11]; break;
	  case MRB_TT_STRING: return ttname[12]; break;
	  case MRB_TT_RANGE: return ttname[13]; break;
	  case MRB_TT_HASH: return ttname[14]; break;
	  default: return ttname[15];
	}
}


void print_vm_info(mrb_vm *vm){
#ifndef RDEBUG
	return;
#endif
//	printf("  [VMINFO]irep        =%p\n",vm->irep);
//	printf("  [VMINFO]pc_irep     =%p\n",vm->pc_irep);
	printf("  [VMINFO]pc          =%d\n",vm->pc);
	printf("  [VMINFO]current_regs=%d,%d\n",vm->current_regs - vm->regs, (vm->current_regs - vm->regs)/4 );
//	printf("  [VMINFO]current_regs=%p\n",vm->current_regs);
	printf("  [VMINFO]callinfo_top=%d\n",vm->callinfo_top);
	mrb_class* c = vm->target_class;
	printf("  [VMINFO]target_class=%s\n",symid_to_str(c->sym_id));
	//printf("opsend:recver.tt=%s\n",get_tt_name(recv.tt));
    mrb_value *regs = vm->current_regs;
	int i=0;
	for(i=0;i<5;i++){
		printf("  [VMINFO]current_reg[%d].tt=%s\n",i,get_tt_name(regs[i].tt));
	}
	for(i=0;i<7;i++){
		printf("  [VMINFO]vm.reg[%d].tt=%s\n",i,get_tt_name(vm->regs[i].tt));
	}
}
static char* opcode_name[]={
"OP_NOP",
"OP_MOVE",
"OP_LOADL",
"OP_LOADI",
"OP_LOADSYM",
"OP_LOADNIL",
"OP_LOADSELF",
"OP_LOADT",
"OP_LOADF",
"OP_GETGLOBAL",
"OP_SETGLOBAL",
"OP_GETIV",
"OP_SETIV",
"OP_GETCONST",
"OP_SETCONST",
"OP_GETUPVAR",
"OP_SETUPVAR",
"OP_JMP",
"OP_JMPIF",
"OP_JMPNOT",
"OP_SEND",
"OP_SENDB",  // reuse
"OP_CALL",
"OP_ENTER",
"OP_RETURN",
"OP_BLKPUSH",
"OP_ADD",
"OP_ADDI",
"OP_SUB",
"OP_SUBI",
"OP_MUL",
"OP_DIV",
"OP_EQ",
"OP_LT",
"OP_LE",
"OP_GT",
"OP_GE",
"OP_ARRAY",
"OP_STRING",
"OP_STRCAT",
"OP_HASH",
"OP_LAMBDA",
"OP_RANGE",
"OP_CLASS",
"OP_EXEC",
"OP_METHOD",
"OP_TCLASS",
"OP_STOP",
"OP_ABORT",  // reuse
"NOT_SUPPORT"
};
char* get_opcode_name(int opcode){
	int n = 0;
	switch( opcode ) {
	  case OP_NOP:        n= 0; break;
	  case OP_MOVE:       n= 1; break;
	  case OP_LOADL:      n= 2; break;
	  case OP_LOADI:      n= 3; break;
	  case OP_LOADSYM:    n= 4; break;
	  case OP_LOADNIL:    n= 5; break;
	  case OP_LOADSELF:   n= 6; break;
	  case OP_LOADT:      n= 7; break;
	  case OP_LOADF:      n= 8; break;
	  case OP_GETGLOBAL:  n= 9; break;
	  case OP_SETGLOBAL:  n=10; break;
	  case OP_GETIV:      n=11; break;
	  case OP_SETIV:      n=12; break;
	  case OP_GETCONST:   n=13; break;
	  case OP_SETCONST:   n=14; break;
	  case OP_GETUPVAR:   n=15; break;
	  case OP_SETUPVAR:   n=16; break;
	  case OP_JMP:        n=17; break;
	  case OP_JMPIF:      n=18; break;
	  case OP_JMPNOT:     n=19; break;
	  case OP_SEND:       n=20; break;
	  case OP_SENDB:      n=21; break;  // reuse
	  case OP_CALL:       n=22; break;
	  case OP_ENTER:      n=23; break;
	  case OP_RETURN:     n=24; break;
	  case OP_BLKPUSH:    n=25; break;
	  case OP_ADD:        n=26; break;
	  case OP_ADDI:       n=27; break;
	  case OP_SUB:        n=28; break;
	  case OP_SUBI:       n=29; break;
	  case OP_MUL:        n=30; break;
	  case OP_DIV:        n=31; break;
	  case OP_EQ:         n=32; break;
	  case OP_LT:         n=33; break;
	  case OP_LE:         n=34; break;
	  case OP_GT:         n=35; break;
	  case OP_GE:         n=36; break;
	  case OP_ARRAY:      n=37; break;
	  case OP_STRING:     n=38; break;
	  case OP_STRCAT:     n=39; break;
	  case OP_HASH:       n=40; break;
	  case OP_LAMBDA:     n=41; break;
	  case OP_RANGE:      n=42; break;
	  case OP_CLASS:      n=43; break;
	  case OP_EXEC:       n=44; break;
	  case OP_METHOD:     n=45; break;
	  case OP_TCLASS:     n=46; break;
	  case OP_STOP:       n=47; break;
	  case OP_ABORT:      n=48; break;  // reuse
	  default:
		n=49;
		break;
	}
	return opcode_name[n];
}

//================================================================
/*! Number of leading zeros.

  @param	x	target (32bit unsined)
  @retval	int	nlz value
*/
static inline int nlz32(uint32_t x)
{
  if( x == 0 ) return 32;

  int n = 1;
  if((x >> 16) == 0 ) { n += 16; x <<= 16; }
  if((x >> 24) == 0 ) { n +=  8; x <<=  8; }
  if((x >> 28) == 0 ) { n +=  4; x <<=  4; }
  if((x >> 30) == 0 ) { n +=  2; x <<=  2; }
  return n - (x >> 31);
}


//================================================================
/*! get sym[n] from symbol table in irep

  @param  p	Pointer to IREP SYMS section.
  @param  n	n th
  @return	symbol name string
*/
const char * mrbc_get_irep_symbol( const uint8_t *p, int n )
{
  int cnt = bin_to_uint32(p);
  if( n >= cnt ) return 0;
  p += 4;
  while( n > 0 ) {
    uint16_t s = bin_to_uint16(p);
    p += 2+s+1;   // size(2 bytes) + symbol len + '\0'
    n--;
  }
  return (char *)p+2;  // skip size(2 bytes)
}


//================================================================
/*! get callee name

  @param  vm	Pointer to VM
  @return	string
*/
const char *mrbc_get_callee_name( mrb_vm *vm )
{
  uint32_t code = bin_to_uint32(vm->pc_irep->code + (vm->pc - 1) * 4);
  int rb = GETARG_B(code);  // index of method sym
  return mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
}


//================================================================
/*!@brief

*/
static void not_supported(void)
{
  console_printf("Not supported!\n");
}



//================================================================
/*!@brief
  Push current status to callinfo stack

*/
void mrbc_push_callinfo(mrb_vm *vm, int n_args)
{
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top;
  callinfo->current_regs = vm->current_regs;
  callinfo->pc_irep = vm->pc_irep;
  callinfo->pc = vm->pc;
  callinfo->n_args = n_args;
  callinfo->target_class = vm->target_class;
  vm->callinfo_top++;
}



//================================================================
/*!@brief
  Push current status to callinfo stack

*/
void mrbc_pop_callinfo(mrb_vm *vm)
{
  vm->callinfo_top--;
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top;
  vm->current_regs = callinfo->current_regs;
  vm->pc_irep = callinfo->pc_irep;
  vm->pc = callinfo->pc;
  vm->target_class = callinfo->target_class;
}



//================================================================
/*!@brief
  Execute OP_NOP

  No operation

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_nop( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  return 0;
}


//================================================================
/*!@brief
  Execute OP_MOVE

  R(A) := R(B)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_move( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
#ifdef RDEBUG
	printf("MOVE R[%d] < R[%d]\n",ra,rb);
#endif
  mrbc_release(&regs[ra]);
  mrbc_dup(&regs[rb]);
  regs[ra] = regs[rb];

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADL

  R(A) := Pool(Bx)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadl( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);

  mrbc_release(&regs[ra]);

  // regs[ra] = vm->pc_irep->pools[rb];

  mrb_object *pool_obj = vm->pc_irep->pools[rb];
  regs[ra] = *pool_obj;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADI

  R(A) := sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadi( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(&regs[ra]);
  regs[ra].tt = MRB_TT_FIXNUM;
  regs[ra].i = GETARG_sBx(code);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADSYM

  R(A) := Syms(Bx)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadsym( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name);

  mrbc_release(&regs[ra]);
  regs[ra].tt = MRB_TT_SYMBOL;
  regs[ra].i = sym_id;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADNIL

  R(A) := nil

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadnil( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(&regs[ra]);
  regs[ra].tt = MRB_TT_NIL;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADSELF

  R(A) := self

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadself( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
#ifdef RDEBUG
	printf("LOADSELF R[%d] = Self\n",ra);
#endif
  mrbc_release(&regs[ra]);
  mrbc_dup(&regs[0]);       // TODO: Need?
  regs[ra] = regs[0];

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADT

  R(A) := true

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadt( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(&regs[ra]);
  regs[ra].tt = MRB_TT_TRUE;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LOADF

  R(A) := false

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_loadf( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(&regs[ra]);
  regs[ra].tt = MRB_TT_FALSE;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_GETGLOBAL

  R(A) := getglobal(Syms(Bx))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_getglobal( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name);

  mrbc_release(&regs[ra]);
  regs[ra] = global_object_get(sym_id);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_SETGLOBAL

  setglobal(Syms(Bx), R(A))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_setglobal( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name);
  global_object_add(sym_id, regs[ra]);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_GETIV

  R(A) := ivget(Syms(Bx))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_getiv( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
#ifdef RDEBUG
	printf("GETIV R[%d] = ivget(Sym[%d])\n",ra,rb);
#endif
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name+1);	// skip '@'

  mrb_value val = mrbc_instance_getiv(&regs[0], sym_id);

  mrbc_release(&regs[ra]);
  regs[ra] = val;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_SETIV

  ivset(Syms(Bx),R(A))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_setiv( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
#ifdef RDEBUG
	printf("SETIV ivset(Sym[%d],R[%d])\n",rb,ra);
#endif
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name+1);	// skip '@'

  mrbc_instance_setiv(&regs[0], sym_id, &regs[ra]);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_GETCONST

  R(A) := constget(Syms(Bx))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_getconst( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name);

  mrbc_release(&regs[ra]);
  regs[ra] = const_object_get(sym_id);

  return 0;
}


//================================================================
/*!@brief
  Execute OP_SETCONST

  constset(Syms(Bx),R(A))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/

inline static int op_setconst( mrb_vm *vm, uint32_t code, mrb_value *regs ) {
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name);
  const_object_add(sym_id, &regs[ra]);

  return 0;
}



//================================================================
/*!@brief
  Execute OP_GETUPVAR

  R(A) := uvget(B,C)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_getupvar( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  int rc = GETARG_C(code);   // UP
  
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top - 2 - rc;
  mrb_value *up_regs = callinfo->current_regs;
  
  mrbc_release( &regs[ra] );
  mrbc_dup( &up_regs[rb] );
  regs[ra] = up_regs[rb];
  
  return 0;
}



//================================================================
/*!@brief
  Execute OP_SETUPVAR

  uvset(B,C,R(A))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_setupvar( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  int rc = GETARG_C(code);   // UP
  
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top - 2 - rc;
  mrb_value *up_regs = callinfo->current_regs;

  mrbc_release( &up_regs[rb] );
  mrbc_dup( &regs[ra] );
  up_regs[rb] = regs[ra];

  return 0;
}



//================================================================
/*!@brief
  Execute OP_JMP

  pc += sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_jmp( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  vm->pc += GETARG_sBx(code) - 1;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_JMPIF

  if R(A) pc += sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_jmpif( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  if( regs[GETARG_A(code)].tt > MRB_TT_FALSE ) {
    vm->pc += GETARG_sBx(code) - 1;
  }
  return 0;
}


//================================================================
/*!@brief
  Execute OP_JMPNOT

  if not R(A) pc += sBx

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_jmpnot( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  if( regs[GETARG_A(code)].tt <= MRB_TT_FALSE ) {
    vm->pc += GETARG_sBx(code) - 1;
  }
  return 0;
}


//================================================================
/*!@brief
  Execute OP_SEND / OP_SENDB

  OP_SEND   R(A) := call(R(A),Syms(B),R(A+1),...,R(A+C))
  OP_SENDB  R(A) := call(R(A),Syms(B),R(A+1),...,R(A+C),&R(A+C+1))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_send( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);  // index of method sym
  int rc = GETARG_C(code);  // number of params
  mrb_value recv = regs[ra];
#ifdef RDEBUG
	printf("OP_SEND   R(%d) := call(R(%d),Syms(%d),R(%d+1),...,R(%d+%d))\n",ra,ra,rb,ra,ra,rc);
#endif
	// Block param
  int bidx = ra + rc + 1;
  switch( GET_OPCODE(code) ) {
  case OP_SEND:
    // set nil
    mrbc_release( &regs[bidx] );
    regs[bidx].tt = MRB_TT_NIL;
    break;

  case OP_SENDB:
    // set Proc object
    if( regs[bidx].tt != MRB_TT_NIL && regs[bidx].tt != MRB_TT_PROC ){
      // TODO: fix the following behavior
      // convert to Proc ?
      // raise exceprion in mruby/c ?
      return 0;
    }
    break;

  default:
    break;
  }

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
  mrb_sym sym_id = str_to_symid(sym_name);
  mrb_proc *m = find_method(vm, recv, sym_id);

  if( m == 0 ) {
    console_printf("No method. vtype=%d method='%s'\n", recv.tt, sym_name);
    return 0;
  }

  // m is C func
  if( m->c_func ) {
    m->func(vm, regs + ra, rc);

    int release_reg = ra+rc+1;
    while( release_reg <= bidx ) {
      // mrbc_release(&regs[release_reg]);
      release_reg++;
    }
    return 0;
  }

  // m is Ruby method.
  // callinfo
  mrbc_push_callinfo(vm, rc);

  // target irep
  vm->pc = 0;
  vm->pc_irep = m->irep;

  // new regs
  vm->current_regs += ra;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_CALL

  R(A) := self.call(frame.argc, frame.argv)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_call( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  mrbc_push_callinfo(vm, 0);

  // jump to proc
  vm->pc = 0;
  vm->pc_irep = regs[0].proc->irep;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ENTER

  arg setup according to flags (23=5:5:1:5:5:1:1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_enter( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top - 1;
  uint32_t enter_param = GETARG_Ax(code);
  int def_args = (enter_param >> 13) & 0x1f;  // default args
  int args = (enter_param >> 18) & 0x1f;      // given args
#ifdef RDEBUG
	printf("def_args=%d args=%d\n",def_args,args);
#endif
  if( def_args > 0 ){
    vm->pc += callinfo->n_args - args;
  }
  return 0;
}


//================================================================
/*!@brief
  Execute OP_RETURN

  return R(A) (B=normal,in-block return/break)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_return( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  // return value
  int ra = GETARG_A(code);
  //  if( ra != 0 ){
  mrb_value v = regs[ra];
  mrbc_dup(&v);
  mrbc_release(&regs[0]);
  regs[0] = v;
  //  }
  // restore irep,pc,regs
  vm->callinfo_top--;
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top;
  mrb_value *regs_ptr = vm->current_regs;
  vm->current_regs = callinfo->current_regs;
  // clear regs and restore vm->reg_top
  // while( regs_ptr > callinfo->current_regs ){
  //   mrbc_release(regs_ptr);
  //   regs_ptr->tt = MRB_TT_EMPTY;
  //   regs_ptr--;
  // }
  // restore others
  vm->pc_irep = callinfo->pc_irep;
  vm->pc = callinfo->pc;
  vm->target_class = callinfo->target_class;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_BLKPUSH

  R(A) := block (16=6:1:5:4)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_blkpush( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrb_value *stack = regs + 1;

  if( stack[0].tt == MRB_TT_NIL ){
    return -1;  // EYIELD
  }

  mrbc_release(&regs[ra]);
  mrbc_dup( stack );
  regs[ra] = stack[0];

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ADD

  R(A) := R(A)+R(A+1) (Syms[B]=:+,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_add( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].i += regs[ra+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].d = regs[ra].i + regs[ra+1].d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].d += regs[ra+1].i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].d += regs[ra+1].d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_ADDI

  R(A) := R(A)+C (Syms[B]=:+)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_addi( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    regs[ra].i += GETARG_C(code);
    return 0;
  }

#if MRBC_USE_FLOAT
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    regs[ra].d += GETARG_C(code);
    return 0;
  }
#endif

  not_supported();
  return 0;
}


//================================================================
/*!@brief
  Execute OP_SUB

  R(A) := R(A)-R(A+1) (Syms[B]=:-,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_sub( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].i -= regs[ra+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].d = regs[ra].i - regs[ra+1].d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].d -= regs[ra+1].i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].d -= regs[ra+1].d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(&regs[ra+1]);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_SUBI

  R(A) := R(A)-C (Syms[B]=:-)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_subi( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    regs[ra].i -= GETARG_C(code);
    return 0;
  }

#if MRBC_USE_FLOAT
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    regs[ra].d -= GETARG_C(code);
    return 0;
  }
#endif

  not_supported();
  return 0;
}


//================================================================
/*!@brief
  Execute OP_MUL

  R(A) := R(A)*R(A+1) (Syms[B]=:*)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_mul( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].i *= regs[ra+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].d = regs[ra].i * regs[ra+1].d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].d *= regs[ra+1].i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].d *= regs[ra+1].d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(&regs[ra+1]);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_DIV

  R(A) := R(A)/R(A+1) (Syms[B]=:/)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_div( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Fixnum, Fixnum
      regs[ra].i /= regs[ra+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Fixnum, Float
      regs[ra].tt = MRB_TT_FLOAT;
      regs[ra].d = regs[ra].i / regs[ra+1].d;
      return 0;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {	// in case of Float, Fixnum
      regs[ra].d /= regs[ra+1].i;
      return 0;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {	// in case of Float, Float
      regs[ra].d /= regs[ra+1].d;
      return 0;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(&regs[ra+1]);
  return 0;
}


//================================================================
/*!@brief
  Execute OP_EQ

  R(A) := R(A)==R(A+1)  (Syms[B]=:==,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_eq( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result = mrbc_compare(&regs[ra], &regs[ra+1]);

  mrbc_release(&regs[ra+1]);
  mrbc_release(&regs[ra]);
  regs[ra].tt = result ? MRB_TT_FALSE : MRB_TT_TRUE;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LT

  R(A) := R(A)<R(A+1)  (Syms[B]=:<,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_lt( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].i < regs[ra+1].i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].i < regs[ra+1].d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].d < regs[ra+1].i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].d < regs[ra+1].d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(&regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_LE

  R(A) := R(A)<=R(A+1)  (Syms[B]=:<=,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_le( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].i <= regs[ra+1].i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].i <= regs[ra+1].d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].d <= regs[ra+1].i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].d <= regs[ra+1].d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(&regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_GT

  R(A) := R(A)>=R(A+1) (Syms[B]=:>=,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_gt( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].i > regs[ra+1].i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].i > regs[ra+1].d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].d > regs[ra+1].i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].d > regs[ra+1].d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(&regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Execute OP_GE

  R(A) := R(A)>=R(A+1) (Syms[B]=:>=,C=1)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_ge( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int result;

  if( regs[ra].tt == MRB_TT_FIXNUM ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].i >= regs[ra+1].i;	// in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].i >= regs[ra+1].d;	// in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[ra].tt == MRB_TT_FLOAT ) {
    if( regs[ra+1].tt == MRB_TT_FIXNUM ) {
      result = regs[ra].d >= regs[ra+1].i;	// in case of Float, Fixnum
      goto DONE;
    }
    if( regs[ra+1].tt == MRB_TT_FLOAT ) {
      result = regs[ra].d >= regs[ra+1].d;	// in case of Float, Float
      goto DONE;
    }
#endif
  }

  // other case
  op_send(vm, code, regs);
  mrbc_release(&regs[ra+1]);
  return 0;

DONE:
  regs[ra].tt = result ? MRB_TT_TRUE : MRB_TT_FALSE;
  return 0;
}


//================================================================
/*!@brief
  Create Array object

  R(A) := ary_new(R(B),R(B+1)..R(B+C))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_array( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  int rc = GETARG_C(code);

  mrb_value value = mrbc_array_new(vm, rc);
  if( value.array == NULL ) return -1;	// ENOMEM

  memcpy( value.array->data, &regs[rb], sizeof(mrb_value) * rc );
  memset( &regs[rb], 0, sizeof(mrb_value) * rc );
  value.array->n_stored = rc;

  mrbc_release(&regs[ra]);
  regs[ra] = value;

  return 0;
}


//================================================================
/*!@brief
  Create string object

  R(A) := str_dup(Lit(Bx))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_string( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
#if MRBC_USE_STRING
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);
  mrb_object *pool_obj = vm->pc_irep->pools[rb];

  /* CAUTION: pool_obj->str - 2. see IREP POOL structure. */
  int len = bin_to_uint16(pool_obj->str - 2);
  mrb_value value = mrbc_string_new(vm, pool_obj->str, len);
  if( value.string == NULL ) return -1;		// ENOMEM

  mrbc_release(&regs[ra]);
  regs[ra] = value;

#else
  not_supported();
#endif
  return 0;
}


//================================================================
/*!@brief
  String Catination

  str_cat(R(A),R(B))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_strcat( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
#if MRBC_USE_STRING
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);

  // call "to_s"
  mrb_sym sym_id = str_to_symid("to_s");
  mrb_proc *m;
  m = find_method(vm, regs[ra], sym_id);
  if( m && m->c_func ){
    m->func(vm, regs+ra, 0);
  } 
  m = find_method(vm, regs[rb], sym_id);
  if( m && m->c_func ){
    m->func(vm, regs+rb, 0);
  }

  mrb_value v = mrbc_string_add(vm, &regs[ra], &regs[rb]);
  mrbc_release(&regs[ra]);
  regs[ra] = v;

#else
  not_supported();
#endif
  return 0;
}


//================================================================
/*!@brief
  Create Hash object

  R(A) := hash_new(R(B),R(B+1)..R(B+C))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_hash( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  int rc = GETARG_C(code);

  mrb_value value = mrbc_hash_new(vm, rc);
  if( value.hash == NULL ) return -1;	// ENOMEM

  rc *= 2;
  memcpy( value.hash->data, &regs[rb], sizeof(mrb_value) * rc );
  memset( &regs[rb], 0, sizeof(mrb_value) * rc );
  value.hash->n_stored = rc;

  mrbc_release(&regs[ra]);
  regs[ra] = value;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_LAMBDA

  R(A) := lambda(SEQ[Bz],Cz)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_lambda( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bz(code);      // sequence position in irep list
  // int c = GETARG_C(code);    // TODO: Add flags support for OP_LAMBDA
  mrb_proc *proc = mrbc_rproc_alloc(vm, "(lambda)");

  proc->c_func = 0;
  proc->irep = vm->pc_irep->reps[rb];

  mrbc_release(&regs[ra]);
  regs[ra].tt = MRB_TT_PROC;
  regs[ra].proc = proc;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_RANGE

  R(A) := range_new(R(B),R(B+1),C)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_range( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  int rc = GETARG_C(code);

  mrbc_dup(&regs[rb]);
  mrbc_dup(&regs[rb+1]);

  mrb_value value = mrbc_range_new(vm, &regs[rb], &regs[rb+1], rc);
  if( value.range == NULL ) return -1;		// ENOMEM

  mrbc_release(&regs[ra]);
  regs[ra] = value;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_CLASS

    R(A) := newclass(R(A),Syms(B),R(A+1))
    Syms(B): class name
    R(A+1): super class

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_class( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);

  mrb_irep *cur_irep = vm->pc_irep;
  const char *sym_name = mrbc_get_irep_symbol(cur_irep->ptr_to_sym, rb);
  mrb_class *super = (regs[ra+1].tt == MRB_TT_CLASS) ? regs[ra+1].cls : mrbc_class_object;

  mrb_class *cls = mrbc_define_class(vm, sym_name, super);

  mrb_value ret = {.tt = MRB_TT_CLASS};
  ret.cls = cls;

  regs[ra] = ret;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_EXEC

  R(A) := blockexec(R(A),SEQ[Bx])

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_exec( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_Bx(code);

  mrb_value recv = regs[ra];

  // prepare callinfo
  mrb_callinfo *callinfo = vm->callinfo + vm->callinfo_top;
  callinfo->current_regs = vm->current_regs;
  callinfo->pc_irep = vm->pc_irep;
  callinfo->pc = vm->pc;
  callinfo->target_class = vm->target_class;
  callinfo->n_args = 0;
  vm->callinfo_top++;

  // target irep
  vm->pc = 0;
  vm->pc_irep = vm->irep->reps[rb];

  // new regs
  vm->current_regs += ra;

  vm->target_class = find_class_by_object(vm, &recv);

  return 0;
}



//================================================================
/*!@brief
  Execute OP_METHOD

  R(A).newmethod(Syms(B),R(A+1))

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_method( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);
  int rb = GETARG_B(code);
  mrb_proc *proc = regs[ra+1].proc;

  if( regs[ra].tt == MRB_TT_CLASS ) {
    mrb_class *cls = regs[ra].cls;

    // sym_id : method name
    mrb_irep *cur_irep = vm->pc_irep;
    const char *sym_name = mrbc_get_irep_symbol(cur_irep->ptr_to_sym, rb);
    int sym_id = str_to_symid(sym_name);

    // check same name method
    mrb_proc *p = cls->procs;
    void *pp = &cls->procs;
    while( p != NULL ) {
      if( p->sym_id == sym_id ) break;
      pp = &p->next;
      p = p->next;
    }
    if( p ) {
      // found it.
      *((mrb_proc**)pp) = p->next;
      if( !p->c_func ) {
	mrb_value v = {.tt = MRB_TT_PROC};
	v.proc = p;
	mrbc_release(&v);
      }
    }

    // add proc to class
    proc->c_func = 0;
    proc->sym_id = sym_id;
#ifdef MRBC_DEBUG
    proc->names = sym_name;		// debug only.
#endif
    proc->next = cls->procs;
    cls->procs = proc;

    mrbc_set_vm_id(proc, 0);
    regs[ra+1].tt = MRB_TT_EMPTY;
  }

  return 0;
}


//================================================================
/*!@brief
  Execute OP_TCLASS

  R(A) := target_class

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
inline static int op_tclass( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  int ra = GETARG_A(code);

  mrbc_release(&regs[ra]);
  regs[ra].tt = MRB_TT_CLASS;
  regs[ra].cls = vm->target_class;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_STOP and OP_ABORT

  stop VM (OP_STOP)
  stop VM without release memory (OP_ABORT)

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval -1  No error and exit from vm.
*/
inline static int op_stop( mrb_vm *vm, uint32_t code, mrb_value *regs )
{
  if( GET_OPCODE(code) == OP_STOP ) {
    int i;
    for( i = 0; i < MAX_REGS_SIZE; i++ ) {
      mrbc_release(&vm->regs[i]);
    }
  }

  vm->flag_preemption = 1;
  
  return -1;
}


//================================================================
/*!@brief
  Open the VM.

  @param vm     Pointer to mrb_vm or NULL.
  @return	Pointer to mrb_vm.
  @retval NULL	error.
*/
mrb_vm *mrbc_vm_open(mrb_vm *vm_arg)
{
  mrb_vm *vm;
  if( (vm = vm_arg) == NULL ) {
    // allocate memory.
    vm = (mrb_vm *)mrbc_raw_alloc( sizeof(mrb_vm) );
    if( vm == NULL ) return NULL;
  }

  // allocate vm id.
  int vm_id = 0;
  int i;
  for( i = 0; i < Num(free_vm_bitmap); i++ ) {
    int n = nlz32( ~free_vm_bitmap[i] );
    if( n < FREE_BITMAP_WIDTH ) {
      free_vm_bitmap[i] |= (1 << (FREE_BITMAP_WIDTH - n - 1));
      vm_id = i * FREE_BITMAP_WIDTH + n + 1;
      break;
    }
  }
  if( vm_id == 0 ) {
    if( vm_arg == NULL ) mrbc_raw_free(vm);
    return NULL;
  }

  // initialize attributes.
  memset(vm, 0, sizeof(mrb_vm));	// caution: assume NULL is zero.
  if( vm_arg == NULL ) vm->flag_need_memfree = 1;
  vm->vm_id = vm_id;

  return vm;
}



//================================================================
/*!@brief
  Close the VM.

  @param  vm  Pointer to mrb_vm
*/
void mrbc_vm_close(mrb_vm *vm)
{
  // free vm id.
  int i = (vm->vm_id-1) / FREE_BITMAP_WIDTH;
  int n = (vm->vm_id-1) % FREE_BITMAP_WIDTH;
  assert( i < Num(free_vm_bitmap) );
  free_vm_bitmap[i] &= ~(1 << (FREE_BITMAP_WIDTH - n - 1));

  // free irep and vm
  mrbc_irep_free( vm->irep );
  if( vm->flag_need_memfree ) mrbc_raw_free(vm);
}



//================================================================
/*!@brief
  VM initializer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_begin(mrb_vm *vm)
{
  vm->pc_irep = vm->irep;
  vm->pc = 0;
  vm->current_regs = vm->regs;
  memset(vm->regs, 0, sizeof(vm->regs));

  // set self to reg[0]
  vm->regs[0].tt = MRB_TT_CLASS;
  vm->regs[0].cls = mrbc_class_object;

  vm->callinfo_top = 0;
  memset(vm->callinfo, 0, sizeof(vm->callinfo));

  // target_class
  vm->target_class = mrbc_class_object;

  vm->error_code = 0;
  vm->flag_preemption = 0;
}


//================================================================
/*!@brief
  VM finalizer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_end(mrb_vm *vm)
{
  mrbc_global_clear_vm_id();
  mrbc_free_all(vm);
}


//================================================================
/*!@brief
  Fetch a bytecode and execute

  @param  vm    A pointer of VM.
  @retval 0  No error.
*/
int mrbc_vm_run( mrb_vm *vm )
{
  int ret = 0;

  do {
    // get one bytecode
    uint32_t cccc = *(uint32_t*)(vm->pc_irep->code + vm->pc * 4);
    uint32_t code = bin_to_uint32(vm->pc_irep->code + vm->pc * 4);
    vm->pc++;

    // regs
    mrb_value *regs = vm->current_regs;

    // Dispatch
    int opcode = GET_OPCODE(code);
#ifdef RDEBUG
	  print_vm_info(vm);
	  printf("[OPCODE]%s %08x -> %08x\n",get_opcode_name(opcode),cccc,code);
#endif	  
	  switch( opcode ) {
    case OP_NOP:        ret = op_nop       (vm, code, regs); break;
    case OP_MOVE:       ret = op_move      (vm, code, regs); break;
    case OP_LOADL:      ret = op_loadl     (vm, code, regs); break;
    case OP_LOADI:      ret = op_loadi     (vm, code, regs); break;
    case OP_LOADSYM:    ret = op_loadsym   (vm, code, regs); break;
    case OP_LOADNIL:    ret = op_loadnil   (vm, code, regs); break;
    case OP_LOADSELF:   ret = op_loadself  (vm, code, regs); break;
    case OP_LOADT:      ret = op_loadt     (vm, code, regs); break;
    case OP_LOADF:      ret = op_loadf     (vm, code, regs); break;
    case OP_GETGLOBAL:  ret = op_getglobal (vm, code, regs); break;
    case OP_SETGLOBAL:  ret = op_setglobal (vm, code, regs); break;
    case OP_GETIV:      ret = op_getiv     (vm, code, regs); break;
    case OP_SETIV:      ret = op_setiv     (vm, code, regs); break;
    case OP_GETCONST:   ret = op_getconst  (vm, code, regs); break;
    case OP_SETCONST:   ret = op_setconst  (vm, code, regs); break;
    case OP_GETUPVAR:   ret = op_getupvar  (vm, code, regs); break;
    case OP_SETUPVAR:   ret = op_setupvar  (vm, code, regs); break;
    case OP_JMP:        ret = op_jmp       (vm, code, regs); break;
    case OP_JMPIF:      ret = op_jmpif     (vm, code, regs); break;
    case OP_JMPNOT:     ret = op_jmpnot    (vm, code, regs); break;
    case OP_SEND:       ret = op_send      (vm, code, regs); break;
    case OP_SENDB:      ret = op_send      (vm, code, regs); break;  // reuse
    case OP_CALL:       ret = op_call      (vm, code, regs); break;
    case OP_ENTER:      ret = op_enter     (vm, code, regs); break;
    case OP_RETURN:     ret = op_return    (vm, code, regs); break;
    case OP_BLKPUSH:    ret = op_blkpush   (vm, code, regs); break;
    case OP_ADD:        ret = op_add       (vm, code, regs); break;
    case OP_ADDI:       ret = op_addi      (vm, code, regs); break;
    case OP_SUB:        ret = op_sub       (vm, code, regs); break;
    case OP_SUBI:       ret = op_subi      (vm, code, regs); break;
    case OP_MUL:        ret = op_mul       (vm, code, regs); break;
    case OP_DIV:        ret = op_div       (vm, code, regs); break;
    case OP_EQ:         ret = op_eq        (vm, code, regs); break;
    case OP_LT:         ret = op_lt        (vm, code, regs); break;
    case OP_LE:         ret = op_le        (vm, code, regs); break;
    case OP_GT:         ret = op_gt        (vm, code, regs); break;
    case OP_GE:         ret = op_ge        (vm, code, regs); break;
    case OP_ARRAY:      ret = op_array     (vm, code, regs); break;
    case OP_STRING:     ret = op_string    (vm, code, regs); break;
    case OP_STRCAT:     ret = op_strcat    (vm, code, regs); break;
    case OP_HASH:       ret = op_hash      (vm, code, regs); break;
    case OP_LAMBDA:     ret = op_lambda    (vm, code, regs); break;
    case OP_RANGE:      ret = op_range     (vm, code, regs); break;
    case OP_CLASS:      ret = op_class     (vm, code, regs); break;
    case OP_EXEC:       ret = op_exec      (vm, code, regs); break;
    case OP_METHOD:     ret = op_method    (vm, code, regs); break;
    case OP_TCLASS:     ret = op_tclass    (vm, code, regs); break;
    case OP_STOP:       ret = op_stop      (vm, code, regs); break;
    case OP_ABORT:      ret = op_stop      (vm, code, regs); break;  // reuse
    default:
      console_printf("Skip OP=%02x\n", GET_OPCODE(code));
      break;
    }
  } while( !vm->flag_preemption );

  vm->flag_preemption = 0;

  return ret;
}
