bits 64

segment .text
global __berry_win_thread_callback

__berry_win_thread_callback:
    ; Allocate shadow space.
    push rbp
    mov rbp, rsp
    sub rsp, 32

    ; The first argument passed to the callback handler is address of the
    ; function we want to call.  Per the win64  calling convention, this
    ; argument is placed in rcx.  We don't need to push or store anything since
    ; it takes no arguments.  Hence, we can just call it directly. 
    call rcx

    ; Windows wants us to return something, so we just return 0.
    xor rax, rax

    leave
    ret

