# if  TARGET_ARCH == PPC_ARCH
/* Sun-$Revision: 30.10 $ */

/* Copyright 1992-2006 Sun Microsystems, Inc. and Stanford University.
   See the LICENSE file for license information. */

# pragma implementation "fields_ppc.hh"

# include "_fields_ppc.cpp.incl"


  
bool is_immediate_pair(inst_t* instp) {
  // find out if instp points to lis followed by ori/addi/load/store
  inst_t i1 = instp[0];
  if (!is_lis(i1)) return false;
  fint reg = RT(i1);
  inst_t i2 = instp[1];
  return
       (is_ori(i2)                    &&  RA(i2) == reg  &&  RS(i2) == reg)
  ||   (is_addi(i2)                   &&  RT(i2) == reg  &&  RS(i2) == reg)
  ||   (is_load_store_immediate(i2)   &&  RA(i2) == reg);
}

  
int32 immediate_pair_target(inst_t* instp) {
  // find out if instp points to lis followed by ori/addi/load/store
  inst_t i2 = instp[1];
  return   is_ori(i2)
             ?    (SI(instp[0]) << si_bits)  |  UI(i2)
             :    (SI(instp[0]) << si_bits)  +  SI(i2);
}


pc_t  get_target_of_branch_instruction(inst_t* instp) {
  switch (OP(*instp)) {
   case opcd_Branch:             return (pc_t) unconditionalImmediateBranch_target(instp);
   case opcd_BranchConditional:  return (pc_t)   conditionalImmediateBranch_target(instp);
   default:                      return (pc_t) NULL;
  }
}


void set_target_of_branch_instruction(inst_t* instp, void* newVal) {
  inst_t nv = inst_t(newVal);
  inst_t inst = *instp;
  
  switch (OP(inst)) {
   case opcd_Branch:             set_unconditionalImmediateBranch_target(instp, nv);  break;
   case opcd_BranchConditional:    set_conditionalImmediateBranch_target(instp, nv);  break;
   default: fatal("not a branch");
  }
}


pc_t get_target_of_C_call_site(inst_t* instp) {
  return get_target_of_branch_instruction(instp);
}


pc_t get_target_of_Self_call_site(inst_t* instp) {
  // given ptr to the instruction before the return PC, return
  // the branch destination
  // WARNING: this routine knows the sequence of instructions generated by
  //   CodeGen::cPrimCall and by CodeGen::selfCall, also
  //   PrimNode::gen()  and SendNode::gen()
  inst_t* ip = instp; // ip points to next instruction to look at (moves backwards)
  
  pc_t r = get_target_of_branch_instruction(ip);
  if ( r != NULL ) {
    --ip; // we have the answer! (in one instruction)
  }  
  else if ( (is_branch_to_ctr(ip[0])  &&  is_mtctr(ip[-1]))
       ||   (is_branch_to_lr (ip[0])  &&  is_mtlr (ip[-1])) ) {
    assert( is_immediate_pair(&ip[-3]), "");
    r = (pc_t) immediate_pair_target(&ip[-3]);
    ip -= 4; // preceding instruction is at ip - 4
  }
  else
    fatal("could not parse branch sequence");    

  // When calling C, Self MAY indirect through SaveSelfNonVolRegs
  if (r == Memory->code->trapdoors->SaveSelfNonVolRegs_td()) {
    --ip;  // back up to first one of pair
    assert( is_immediate_pair(ip), "");
    r = (pc_t) immediate_pair_target(ip);
  }
    
  return r;
}



void set_target_of_Self_call_site(inst_t* instp, void* target) {
  // WARNING: this routine knows the sequence of instructions generated by
  //   CodeGen::cPrimCall and by CodeGen::selfCall, and by call to
  //   SendDIDesc_stub in CodeGen::prologue
  inst_t* ip = instp; // ip points to next instruction to look at (moves backwards)
  pc_t    nv_stub = Memory->code->trapdoors->SaveSelfNonVolRegs_td();
  
  pc_t r = get_target_of_branch_instruction(ip);
  if ( r == nv_stub ) {
    // Calling via this stub, must look further back
    --ip;
  }
  else if ( r != NULL ) {
    // found it!
    set_target_of_branch_instruction(ip, target);
    return;
  }
  // not a simple jump; look for a complex one
  else if ( (is_branch_to_ctr(ip[0])  &&  is_mtctr(ip[-1]))
       ||   (is_branch_to_lr (ip[0])  &&  is_mtlr (ip[-1])) ) {
    ip -= 3;
    assert( is_immediate_pair(ip), "");
    r = (pc_t) immediate_pair_target(ip);
    if ( r != nv_stub ) {
      // found it!
      set_immediate_pair_target(ip, (inst_t)target);
      return;
    }
    --ip; // will look prior to pair
  }
  else
    fatal("could not parse branch sequence");    

  --ip; // start of pair
  assert( is_immediate_pair(ip), "");
  set_immediate_pair_target(ip, (inst_t)target);
}


void set_immediate_pair_target(inst_t *instp, int32 nv) {
  int32 lo, hi;
  inst_t i0 = instp[0];
  inst_t i1 = instp[1];
  if ( is_ori(i1) )   Assembler::break_up_word_for_oring (nv, lo, hi);
  else                Assembler::break_up_word_for_adding(nv, lo, hi);
  instp[0] = (i0 & ~si_mask)  |  hi;
  instp[1] = (i1 & ~si_mask)  |  lo;  assert(si_mask == ui_mask, "");
  MachineCache::flush_instruction_cache_range(&instp[0], &instp[2]);
}


char* address_of_overwritten_NIC_save_instruction(int32* orig_save_addr) {
  if (is_stwu(*orig_save_addr))
    return NULL; // not overwritten
    
  // the instruction must have been patched with a branch;
  assert(isUnconditionalImmediateBranch(*orig_save_addr), "must be an uncond branch");
  return (char*)unconditionalImmediateBranch_target(orig_save_addr);
}


void check_branch_relocation( void* fromArg, void* toArg, int32 countArg) {
  inst_t *from = (inst_t*)fromArg, *to = (inst_t*)toArg;
  int32 count = countArg / sizeof(inst_t);
  
  if ( umax((uint32)from, (uint32)to)  <  umin((uint32)from + count, (uint32) to + count))
   return; // overlapping, too hard to check
  
  for ( int32 i = 0;  i < count;  ++i) {
    inst_t* f = (inst_t*)get_target_of_branch_instruction(&from[i]);
    inst_t* t = (inst_t*)get_target_of_branch_instruction(&  to[i]);

         if (f == NULL)                      ;
    else if (from <= f  &&  f < from+count)  f += to - from;

    if (f != t)
      fatal2("check_branch_relocation: branch at 0x%x moved to 0x%x but is wrong", 
             &from[i], &to[i]);
  }
}

# endif // TARGET_ARCH == PPC_ARCH
