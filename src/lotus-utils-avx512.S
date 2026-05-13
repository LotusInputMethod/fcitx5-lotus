.intel_syntax noprefix
.text

.globl compare_split_avx512
.type compare_split_avx512, @function

# SysV ABI:
# rdi = A
# rsi = B
# rdx = lenA
# rcx = lenB (unused)
# r8  = dummy (unused)
#
# return: rax = first differing index

compare_split_avx512:
    xor rax, rax

.Lloop:
    # check if we can safely load 64 bytes
    mov r10, rax
    add r10, 64
    cmp r10, rdx
    ja .Ltail

    # load 64 bytes from both strings
    vmovdqu64 zmm0, ZMMWORD PTR [rdi + rax]
    vmovdqu64 zmm1, ZMMWORD PTR [rsi + rax]

    # compare bytes
    vpcmpeqb k1, zmm0, zmm1
    kortestq k1, k1
    jnc .Ldiff

    add rax, 64
    jmp .Lloop

.Ldiff:
    # k1 = equal bytes
    # invert to get mismatch mask
    knotq k1, k1

    # move mask to GPR
    kmovq r11, k1

    # find first mismatch byte
    tzcnt r11, r11

    add rax, r11
    ret

.Ltail:
    cmp rax, rdx
    jae .Ldone

.Lbyte_loop:
    mov r10b, BYTE PTR [rdi + rax]
    cmp r10b, BYTE PTR [rsi + rax]
    jne .Ldone

    inc rax
    cmp rax, rdx
    jb .Lbyte_loop

.Ldone:
    ret

.globl utf8_length_avx512
.type utf8_length_avx512, @function

# rdi = str, rsi = len
# returns rax = codepoint count (non-continuation bytes)
utf8_length_avx512:
    xor rax, rax
    xor r10, r10
    mov r11d, 0xC0
    vpbroadcastb zmm2, r11d
    mov r11d, 0x80
    vpbroadcastb zmm3, r11d

.Lu8_loop:
    lea r11, [r10 + 64]
    cmp r11, rsi
    ja .Lu8_tail
    vmovdqu8 zmm0, [rdi + r10]
    vpandq zmm1, zmm0, zmm2
    vpcmpeqb k1, zmm1, zmm3
    kmovq r11, k1
    not r11
    popcnt r11, r11
    add rax, r11
    add r10, 64
    jmp .Lu8_loop

.Lu8_tail:
    cmp r10, rsi
    jae .Lu8_done
.Lu8_byte:
    movzx r11d, BYTE PTR [rdi + r10]
    and r11d, 0xC0
    cmp r11d, 0x80
    je .Lu8_skip
    inc rax
.Lu8_skip:
    inc r10
    cmp r10, rsi
    jb .Lu8_byte
.Lu8_done:
    ret


.globl find_char_avx512
.type find_char_avx512, @function

# rdi = str, rsi = len, rdx = start, rcx = ch
# returns rax = index or (size_t)-1
find_char_avx512:
    mov rax, rdx
    vpbroadcastb zmm1, ecx

.Lfc_loop:
    lea r10, [rax + 64]
    cmp r10, rsi
    ja .Lfc_tail
    vmovdqu8 zmm0, [rdi + rax]
    vpcmpeqb k1, zmm0, zmm1
    kmovq r10, k1
    test r10, r10
    jnz .Lfc_found
    add rax, 64
    jmp .Lfc_loop

.Lfc_found:
    tzcnt r10, r10
    add rax, r10
    cmp rax, rsi
    jb .Lfc_done
    mov rax, -1
.Lfc_done:
    ret

.Lfc_tail:
    cmp rax, rsi
    jae .Lfc_notfound
.Lfc_byte:
    movzx r10d, BYTE PTR [rdi + rax]
    cmp r10b, cl
    je .Lfc_done
    inc rax
    cmp rax, rsi
    jb .Lfc_byte
.Lfc_notfound:
    mov rax, -1
    ret

.globl tolower_avx512
.type tolower_avx512, @function

# rdi = str (in-place), rsi = len
tolower_avx512:
    test rsi, rsi
    jz .Ltl_done
    mov r8d, 'A'
    vpbroadcastb zmm3, r8d
    mov r8d, 'Z'
    vpbroadcastb zmm4, r8d
    mov r8d, 0x20
    vpbroadcastb zmm5, r8d
    xor r10, r10

.Ltl_loop:
    lea r11, [r10 + 64]
    cmp r11, rsi
    ja .Ltl_tail
    vmovdqu8 zmm0, [rdi + r10]
    vpcmpub k1, zmm0, zmm3, 5
    vpcmpub k2, zmm0, zmm4, 2
    kandq k3, k1, k2
    vpaddb zmm1, zmm0, zmm5
    vmovdqu8 zmm0{k3}, zmm1
    vmovdqu8 [rdi + r10], zmm0
    add r10, 64
    jmp .Ltl_loop

.Ltl_tail:
    cmp r10, rsi
    jae .Ltl_done
.Ltl_byte:
    movzx r11d, BYTE PTR [rdi + r10]
    cmp r11d, 'A'
    jb .Ltl_skip
    cmp r11d, 'Z'
    ja .Ltl_skip
    add r11d, 0x20
    mov BYTE PTR [rdi + r10], r11b
.Ltl_skip:
    inc r10
    cmp r10, rsi
    jb .Ltl_byte
.Ltl_done:
    ret


.globl strfind_avx512
.type strfind_avx512, @function

# rdi=haystack, rsi=hlen, rdx=needle, rcx=nlen
# returns rax = index or (size_t)-1
strfind_avx512:
    test rcx, rcx
    jz .Lsf_zero
    cmp rsi, rcx
    jb .Lsf_notfound
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    mov r15, rcx
    mov rbp, r13
    sub rbp, r15            # rbp = max valid start = hlen - nlen
    movzx eax, BYTE PTR [r14]
    vpbroadcastb zmm2, eax
    xor r11, r11            # scan offset

.Lsf_loop:
    cmp r11, rbp
    ja .Lsf_miss
    lea rax, [r11 + 64]
    cmp rax, r13
    ja .Lsf_scalar
    vmovdqu8 zmm0, [r12 + r11]
    vpcmpeqb k1, zmm0, zmm2
    kmovq rbx, k1

.Lsf_mask:
    test rbx, rbx
    jz .Lsf_advance
    tzcnt rax, rbx
    add rax, r11
    cmp rax, rbp
    ja .Lsf_miss
    xor r8, r8
.Lsf_cmp:
    cmp r8, r15
    jae .Lsf_hit
    lea r10, [r12 + rax]
    movzx r9d, BYTE PTR [r10 + r8]
    cmp r9b, BYTE PTR [r14 + r8]
    jne .Lsf_cmp_fail
    inc r8
    jmp .Lsf_cmp
.Lsf_cmp_fail:
    blsr rbx, rbx
    jmp .Lsf_mask
.Lsf_advance:
    add r11, 64
    jmp .Lsf_loop

.Lsf_scalar:
    cmp r11, rbp
    ja .Lsf_miss
    movzx r9d, BYTE PTR [r12 + r11]
    cmp r9b, BYTE PTR [r14]
    jne .Lsf_scalar_next
    xor r8, r8
.Lsf_scalar_cmp:
    cmp r8, r15
    jae .Lsf_hit_r11
    lea r10, [r12 + r11]
    movzx r9d, BYTE PTR [r10 + r8]
    cmp r9b, BYTE PTR [r14 + r8]
    jne .Lsf_scalar_next
    inc r8
    jmp .Lsf_scalar_cmp
.Lsf_hit_r11:
    mov rax, r11
    jmp .Lsf_hit
.Lsf_scalar_next:
    inc r11
    jmp .Lsf_scalar

.Lsf_hit:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.Lsf_miss:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
.Lsf_notfound:
    mov rax, -1
    ret
.Lsf_zero:
    xor rax, rax
    ret
