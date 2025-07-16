	.file	"converter.c"
	.text
	.globl	version
	.section .rdata,"dr"
.LC0:
	.ascii "0001\0"
	.data
	.align 8
version:
	.quad	.LC0
	.globl	Sign
	.section .rdata,"dr"
.LC1:
	.ascii "AS&AN\0"
	.data
	.align 8
Sign:
	.quad	.LC1
	.globl	Sign_WNL
	.section .rdata,"dr"
.LC2:
	.ascii "AS&AN\12\0"
	.data
	.align 8
Sign_WNL:
	.quad	.LC2
	.globl	Sign_Size
	.align 4
Sign_Size:
	.long	8
	.globl	Sign_Size_WNL
	.align 4
Sign_Size_WNL:
	.long	8
	.section .rdata,"dr"
	.align 8
.LC3:
	.ascii "Starting Conversion of file- %s\12\0"
.LC4:
	.ascii "Checking options\0"
.LC5:
	.ascii "-d\0"
.LC6:
	.ascii ":\0"
.LC7:
	.ascii "Debug Level set to - %s \12\0"
	.text
	.globl	main
	.def	main;	.scl	2;	.type	32;	.endef
	.seh_proc	main
main:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$96, %rsp
	.seh_stackalloc	96
	.seh_endprologue
	movl	%ecx, 16(%rbp)
	movq	%rdx, 24(%rbp)
	call	__main
	movq	24(%rbp), %rax
	addq	$8, %rax
	movq	(%rax), %rax
	movq	%rax, %rdx
	leaq	.LC3(%rip), %rax
	movq	%rax, %rcx
	call	printf
	leaq	.LC4(%rip), %rax
	movq	%rax, %rcx
	call	puts
	movl	$0, -4(%rbp)
	movq	24(%rbp), %rax
	movq	8(%rax), %rax
	movq	%rax, -24(%rbp)
	cmpl	$2, 16(%rbp)
	jle	.L2
	movq	24(%rbp), %rax
	addq	$16, %rax
	movq	(%rax), %rax
	leaq	.LC5(%rip), %rdx
	movq	%rax, %rcx
	call	strstr
	testq	%rax, %rax
	je	.L2
	movl	$0, -8(%rbp)
	movq	24(%rbp), %rax
	addq	$16, %rax
	movq	(%rax), %rax
	leaq	.LC6(%rip), %rdx
	movq	%rax, %rcx
	call	strtok
	movq	%rax, -16(%rbp)
	jmp	.L3
.L4:
	movl	-8(%rbp), %eax
	leal	1(%rax), %edx
	movl	%edx, -8(%rbp)
	cltq
	movq	-16(%rbp), %rdx
	movq	%rdx, -64(%rbp,%rax,8)
	leaq	.LC6(%rip), %rax
	movq	%rax, %rdx
	movl	$0, %ecx
	call	strtok
	movq	%rax, -16(%rbp)
.L3:
	cmpq	$0, -16(%rbp)
	jne	.L4
	movq	-56(%rbp), %rax
	movq	%rax, %rcx
	call	atoi
	movl	%eax, -4(%rbp)
	movq	-56(%rbp), %rax
	movq	%rax, %rdx
	leaq	.LC7(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L2:
	movl	-4(%rbp), %edx
	movq	-24(%rbp), %rax
	movq	%rax, %rcx
	call	check_sign
	movl	%eax, -28(%rbp)
	cmpl	$1, -28(%rbp)
	jne	.L5
	movl	$1, %eax
	jmp	.L6
.L5:
	movl	-4(%rbp), %edx
	movq	-24(%rbp), %rax
	movq	%rax, %rcx
	call	check_ext
	movl	%eax, -28(%rbp)
	cmpl	$1, -28(%rbp)
	jne	.L7
	movl	$1, %eax
	jmp	.L6
.L7:
	movl	$0, %eax
.L6:
	addq	$96, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.section .rdata,"dr"
.LC8:
	.ascii "r\0"
.LC9:
	.ascii "\0"
	.align 8
.LC10:
	.ascii "Opened file in mode [READ] at [%s]. Buffer size set to [%i]\12\0"
.LC11:
	.ascii "File Data -> %s\12\0"
	.text
	.globl	Read_File
	.def	Read_File;	.scl	2;	.type	32;	.endef
	.seh_proc	Read_File
Read_File:
	pushq	%rbp
	.seh_pushreg	%rbp
	pushq	%rsi
	.seh_pushreg	%rsi
	pushq	%rbx
	.seh_pushreg	%rbx
	subq	$64, %rsp
	.seh_stackalloc	64
	leaq	64(%rsp), %rbp
	.seh_setframe	%rbp, 64
	.seh_endprologue
	movq	%rcx, 32(%rbp)
	movl	%edx, 40(%rbp)
	movl	%r8d, 48(%rbp)
	movl	%r9d, %eax
	movb	%al, 56(%rbp)
	movq	%rsp, %rax
	movq	%rax, %rsi
	movq	32(%rbp), %rax
	leaq	.LC8(%rip), %rdx
	movq	%rax, %rcx
	call	fopen
	movq	%rax, -16(%rbp)
	movl	48(%rbp), %eax
	movslq	%eax, %rdx
	subq	$1, %rdx
	movq	%rdx, -24(%rbp)
	cltq
	addq	$15, %rax
	shrq	$4, %rax
	salq	$4, %rax
	call	___chkstk_ms
	subq	%rax, %rsp
	leaq	32(%rsp), %rax
	movq	%rax, -32(%rbp)
	leaq	.LC9(%rip), %rax
	movq	%rax, -8(%rbp)
	cmpl	$0, 40(%rbp)
	jle	.L9
	movl	48(%rbp), %edx
	movq	32(%rbp), %rax
	movl	%edx, %r8d
	movq	%rax, %rdx
	leaq	.LC10(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L9:
	movzbl	56(%rbp), %eax
	xorl	$1, %eax
	testb	%al, %al
	je	.L10
	jmp	.L11
.L12:
	movq	-32(%rbp), %rdx
	movq	-8(%rbp), %rax
	movq	%rax, %rcx
	call	strcat
	movq	%rax, -8(%rbp)
.L11:
	movq	-16(%rbp), %rcx
	movl	48(%rbp), %edx
	movq	-32(%rbp), %rax
	movq	%rcx, %r8
	movq	%rax, %rcx
	call	fgets
	testq	%rax, %rax
	setne	%bl
	movq	-16(%rbp), %rax
	movq	%rax, %rcx
	call	fgetc
	cmpl	$-1, %eax
	setne	%al
	orl	%ebx, %eax
	testb	%al, %al
	jne	.L12
	jmp	.L13
.L10:
	movq	-16(%rbp), %rcx
	movl	48(%rbp), %edx
	movq	-32(%rbp), %rax
	movq	%rcx, %r8
	movq	%rax, %rcx
	call	fgets
	movq	-32(%rbp), %rax
	movq	%rax, -8(%rbp)
.L13:
	cmpl	$2, 40(%rbp)
	jle	.L14
	movq	-8(%rbp), %rax
	movq	%rax, %rdx
	leaq	.LC11(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L14:
	movq	-16(%rbp), %rax
	movq	%rax, %rcx
	call	fclose
	movq	-8(%rbp), %rax
	movq	%rsi, %rsp
	movq	%rbp, %rsp
	popq	%rbx
	popq	%rsi
	popq	%rbp
	ret
	.seh_endproc
	.section .rdata,"dr"
.LC12:
	.ascii "RECIEVED FILEPATH - %s\12\0"
.LC13:
	.ascii "Required Sign -> [%s]\12\0"
.LC14:
	.ascii "Sign Approved!\0"
.LC15:
	.ascii "Sign Failed! [%s]\12\0"
	.text
	.globl	check_sign
	.def	check_sign;	.scl	2;	.type	32;	.endef
	.seh_proc	check_sign
check_sign:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$48, %rsp
	.seh_stackalloc	48
	.seh_endprologue
	movq	%rcx, 16(%rbp)
	movl	%edx, 24(%rbp)
	cmpl	$2, 24(%rbp)
	jle	.L17
	movq	16(%rbp), %rax
	movq	%rax, %rdx
	leaq	.LC12(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L17:
	movl	24(%rbp), %edx
	movq	16(%rbp), %rax
	movl	$1, %r9d
	movl	$1024, %r8d
	movq	%rax, %rcx
	call	Read_File
	movq	%rax, -8(%rbp)
	cmpl	$3, 24(%rbp)
	jle	.L18
	movq	Sign_WNL(%rip), %rax
	movq	%rax, %rdx
	leaq	.LC13(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L18:
	movq	Sign_WNL(%rip), %rax
	cmpq	%rax, -8(%rbp)
	jne	.L19
	cmpl	$0, 24(%rbp)
	jle	.L20
	leaq	.LC14(%rip), %rax
	movq	%rax, %rcx
	call	puts
.L20:
	movl	$0, %eax
	jmp	.L21
.L19:
	movq	-8(%rbp), %rax
	movq	%rax, %rdx
	leaq	.LC15(%rip), %rax
	movq	%rax, %rcx
	call	printf
	movl	$1, %eax
.L21:
	addq	$48, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.section .rdata,"dr"
.LC16:
	.ascii ".\0"
.LC17:
	.ascii "New Path: %s \12\0"
	.text
	.globl	convert_ext
	.def	convert_ext;	.scl	2;	.type	32;	.endef
	.seh_proc	convert_ext
convert_ext:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$96, %rsp
	.seh_stackalloc	96
	.seh_endprologue
	movq	%rcx, 16(%rbp)
	movl	%edx, 24(%rbp)
	movq	%r8, 32(%rbp)
	movl	$0, -4(%rbp)
	movq	16(%rbp), %rax
	leaq	.LC16(%rip), %rdx
	movq	%rax, %rcx
	call	strtok
	movq	%rax, -16(%rbp)
	jmp	.L23
.L24:
	movl	-4(%rbp), %eax
	leal	1(%rax), %edx
	movl	%edx, -4(%rbp)
	cltq
	movq	-16(%rbp), %rdx
	movq	%rdx, -64(%rbp,%rax,8)
	leaq	.LC16(%rip), %rax
	movq	%rax, %rdx
	movl	$0, %ecx
	call	strtok
	movq	%rax, -16(%rbp)
.L23:
	cmpq	$0, -16(%rbp)
	jne	.L24
	movq	-64(%rbp), %rax
	movq	%rax, -24(%rbp)
	movq	32(%rbp), %rdx
	movq	-24(%rbp), %rax
	movq	%rax, %rcx
	call	strcat
	movq	%rax, -32(%rbp)
	cmpl	$0, 24(%rbp)
	jle	.L25
	movq	-32(%rbp), %rax
	movq	%rax, %rdx
	leaq	.LC17(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L25:
	movq	-32(%rbp), %rax
	addq	$96, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.section .rdata,"dr"
.LC18:
	.ascii "aef\0"
.LC19:
	.ascii ".aef\0"
.LC20:
	.ascii "Correct extention - %s \12\0"
.LC21:
	.ascii "Wrong extention - %s \12\0"
.LC22:
	.ascii "Quitting... \0"
	.text
	.globl	check_ext
	.def	check_ext;	.scl	2;	.type	32;	.endef
	.seh_proc	check_ext
check_ext:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$80, %rsp
	.seh_stackalloc	80
	.seh_endprologue
	movq	%rcx, 16(%rbp)
	movl	%edx, 24(%rbp)
	movl	$0, -4(%rbp)
	movq	16(%rbp), %rax
	leaq	.LC16(%rip), %rdx
	movq	%rax, %rcx
	call	strtok
	movq	%rax, -16(%rbp)
	jmp	.L28
.L29:
	movl	-4(%rbp), %eax
	leal	1(%rax), %edx
	movl	%edx, -4(%rbp)
	cltq
	movq	-16(%rbp), %rdx
	movq	%rdx, -48(%rbp,%rax,8)
	leaq	.LC16(%rip), %rax
	movq	%rax, %rdx
	movl	$0, %ecx
	call	strtok
	movq	%rax, -16(%rbp)
.L28:
	cmpq	$0, -16(%rbp)
	jne	.L29
	movq	-40(%rbp), %rax
	movq	%rax, -24(%rbp)
	movq	-24(%rbp), %rax
	leaq	.LC18(%rip), %rdx
	movq	%rax, %rcx
	call	strstr
	testq	%rax, %rax
	je	.L30
	cmpl	$0, 24(%rbp)
	js	.L31
	leaq	.LC19(%rip), %rax
	movq	%rax, %rdx
	leaq	.LC20(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L31:
	movl	$0, %eax
	jmp	.L34
.L30:
	cmpl	$0, 24(%rbp)
	js	.L33
	movq	-24(%rbp), %rax
	addq	$1, %rax
	movzbl	(%rax), %eax
	movsbl	%al, %eax
	movl	%eax, %edx
	leaq	.LC21(%rip), %rax
	movq	%rax, %rcx
	call	printf
.L33:
	leaq	.LC22(%rip), %rax
	movq	%rax, %rcx
	call	puts
	movl	$1, %eax
.L34:
	addq	$80, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.def	__main;	.scl	2;	.type	32;	.endef
	.ident	"GCC: (Rev3, Built by MSYS2 project) 14.1.0"
	.def	printf;	.scl	2;	.type	32;	.endef
	.def	puts;	.scl	2;	.type	32;	.endef
	.def	strstr;	.scl	2;	.type	32;	.endef
	.def	strtok;	.scl	2;	.type	32;	.endef
	.def	atoi;	.scl	2;	.type	32;	.endef
	.def	fopen;	.scl	2;	.type	32;	.endef
	.def	strcat;	.scl	2;	.type	32;	.endef
	.def	fgets;	.scl	2;	.type	32;	.endef
	.def	fgetc;	.scl	2;	.type	32;	.endef
	.def	fclose;	.scl	2;	.type	32;	.endef
