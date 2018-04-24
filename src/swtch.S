# Context switch
#
#   void swtch(struct context **old, struct context *new);
# 
# Save the current registers on the stack, creating
# a struct context, and save its address in *old.
# Switch stacks to new and pop previously-saved registers.

# swtch(&p->context, mycpu()->scheduler);
# movl 4(%esp), %eax -> new
# movl 8(%esp), %eax -> old pointer
.globl swtch
swtch:
  movl 4(%esp), %eax
  movl 8(%esp), %edx

  # Save old callee-save registers
  # push old
  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi

  # Switch stacks
  # let eax(old context pointer) point to esp(current top of stack), to make sure the next time into old context execute from there
  # load edx (new context) to esp, and execute the new context next stream
  movl %esp, (%eax)
  movl %edx, %esp

  # Load new callee-save registers
  # pop new
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret
