// See LICENSE for license details.

#include "pcr.h"
#include "pk.h"
#include "config.h"
#include "syscall.h"
#include "vm.h"

int have_fp = 1; // initialized to 1 because it can't be in the .bss section!
int have_vector = 1;
  
static void handle_vector_disabled(trapframe_t* tf)
{
  if (have_vector)
    tf->sr |= SR_EV;
  else
    panic("No vector hardware! pc %lx, insn %x",tf->epc,(uint32_t)tf->insn);
}

static void handle_vector_bank(trapframe_t* tf)
{
  dump_tf(tf);
  panic("Not enought banks were enabled to execute a vector instruction!");
}

static void handle_vector_illegal_instruction(trapframe_t* tf)
{
  dump_tf(tf);
  panic("An illegal vector instruction was executed!");
}

static void handle_privileged_instruction(trapframe_t* tf)
{
  dump_tf(tf);
  panic("A privileged instruction was executed!");
}

static void handle_illegal_instruction(trapframe_t* tf)
{
#ifdef PK_ENABLE_FP_EMULATION
  if(emulate_fp(tf) == 0)
  {
    advance_pc(tf);
    return;
  }
#endif

  if(emulate_int(tf) == 0)
  {
    advance_pc(tf);
    return;
  }

  dump_tf(tf);
  panic("An illegal instruction was executed!");
}

static void handle_fp_disabled(trapframe_t* tf)
{
  if(have_fp && !(mfpcr(PCR_SR) & SR_EF))
    init_fp(tf);
  else
    handle_illegal_instruction(tf);
}

static void handle_breakpoint(trapframe_t* tf)
{
  dump_tf(tf);
  printk("Breakpoint!\n");
}

static void handle_misaligned_fetch(trapframe_t* tf)
{
  dump_tf(tf);
  panic("Misaligned instruction access!");
}

void handle_misaligned_load(trapframe_t* tf)
{
  // TODO emulate misaligned loads and stores
  dump_tf(tf);
  panic("Misaligned load!");
}

void handle_misaligned_store(trapframe_t* tf)
{
  dump_tf(tf);
  panic("Misaligned store!");
}

static void segfault(trapframe_t* tf, uintptr_t addr, const char* type)
{
  dump_tf(tf);
  const char* who = (tf->sr & SR_PS) ? "Kernel" : "User";
  panic("%s %s segfault @ %p", who, type, addr);
}

static void handle_fault_fetch(trapframe_t* tf)
{
  if (handle_page_fault(tf->epc, PROT_EXEC) != 0)
    segfault(tf, tf->epc, "fetch");
}

void handle_fault_load(trapframe_t* tf)
{
  if (handle_page_fault(tf->badvaddr, PROT_READ) != 0)
    segfault(tf, tf->badvaddr, "load");
}

void handle_fault_store(trapframe_t* tf)
{
  if (handle_page_fault(tf->badvaddr, PROT_WRITE) != 0)
    segfault(tf, tf->badvaddr, "store");
}

static void handle_syscall(trapframe_t* tf)
{
  sysret_t ret = syscall(tf->gpr[18], tf->gpr[19], tf->gpr[20], tf->gpr[21],
                         tf->gpr[22], tf->gpr[23], tf->gpr[16]);

  tf->gpr[16] = ret.result;
  tf->gpr[21] = ret.err;

  advance_pc(tf);
}

void handle_trap(trapframe_t* tf)
{
  setpcr(PCR_SR, SR_EI);

  typedef void (*trap_handler)(trapframe_t*);

  const static trap_handler trap_handlers[] = {
    [CAUSE_MISALIGNED_FETCH] = handle_misaligned_fetch,
    [CAUSE_FAULT_FETCH] = handle_fault_fetch,
    [CAUSE_ILLEGAL_INSTRUCTION] = handle_illegal_instruction,
    [CAUSE_PRIVILEGED_INSTRUCTION] = handle_privileged_instruction,
    [CAUSE_FP_DISABLED] = handle_fp_disabled,
    [CAUSE_SYSCALL] = handle_syscall,
    [CAUSE_BREAKPOINT] = handle_breakpoint,
    [CAUSE_MISALIGNED_LOAD] = handle_misaligned_load,
    [CAUSE_MISALIGNED_STORE] = handle_misaligned_store,
    [CAUSE_FAULT_LOAD] = handle_fault_load,
    [CAUSE_FAULT_STORE] = handle_fault_store,
    [CAUSE_VECTOR_DISABLED] = handle_vector_disabled,
    [CAUSE_VECTOR_BANK] = handle_vector_bank,
    [CAUSE_VECTOR_ILLEGAL_INSTRUCTION] = handle_vector_illegal_instruction,
  };

  kassert(tf->cause < ARRAY_SIZE(trap_handlers) && trap_handlers[tf->cause]);

  trap_handlers[tf->cause](tf);

  pop_tf(tf);
}
