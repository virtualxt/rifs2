;----------------------------------------------------------------------
;----------------------------------------------------------------------
;
; IFS client module assembly stubs
;
;----------------------------------------------------------------------
;----------------------------------------------------------------------
IFS0_TEXT	segment	byte public 'CODE'
DGROUP	group	_DATA,_BSS
	assume	cs:IFS0_TEXT,ds:DGROUP
IFS0_TEXT	ends

_DATA	segment word public 'DATA'
d@	label	byte
d@w	label	word

userintno	db	?

STK1LEN 	equ	512
stk1		db	STK1LEN dup (0)
stk1end		equ	$
stk1sav		dd	?
_DATA	ends

_BSS	segment word public 'BSS'
b@	label	byte
b@w	label	word
_BSS	ends

extrn	_dispatch:far
extrn	_UserInt:far

public  _ifs0_init
public  _ifs0_shutdown
public  _ifs0_GetStackUsed

IFS0_TEXT	segment	byte public 'CODE'

_oldint2f	dd	?
_olduser	dd	?

;------------------------------------------------------------
; void far ifs0_init(unsigned itr)
;   install our IFS hook. set user communications hook to 
;   [itr]
;------------------------------------------------------------
_ifs0_init	proc	far
		push	bp			; standard entry 
		mov	bp,sp
		mov	ax,[bp+6]		; ax = itr
		mov	[userintno],al
		mov	ax,352fh		; get Multiplex int.
		int	21h
		mov word ptr cs:[_oldint2f], bx
		mov word ptr cs:[_oldint2f+2], es
		
		mov	ah,35h			; get user hook
		mov	al,[userintno]
		int	21H
		mov word ptr cs:[_olduser], bx
		mov word ptr cs:[_olduser+2], es
		
		mov	bl,[userintno]		; bl = user int #
		push	ds			; set our hooks
		push	cs
		pop	ds
		mov	dx, offset _ifs0
		mov	ax,252fh
		int	21h
		
		mov	dx, offset _ifs0_user
		mov	ah,25h
		mov	al,bl
		int	21h
		
		pop	ds
		pop	bp
		ret
_ifs0_init	endp

;------------------------------------------------------------
; void far ifs0_shutdown(void)
;    restore intecepted ints
;------------------------------------------------------------
_ifs0_shutdown	proc	far
		mov	al,[userintno]
		push	ds			; restore orig. hooks
		lds	dx, cs:[_olduser]
		mov	ah, 25h
		int	21h
		
		lds	dx, cs:[_oldint2f]
		mov	ax, 252fh
		int	21h
		
		pop	ds
		ret
_ifs0_shutdown	endp

here		db	0		; i doubt this is necessary
					; but better safe than sorry :)
_ifs0	   	proc far
		push	ax
		cmp	cs:[here],0	; are we already here?
		jnz	@@notus		; yes, exit
		mov	cs:[here],1	; set sem.
		push	bx		; save remaining registers
		push	cx
		push	dx
		push	si
		push	di
		push	ds
		push	es
		push	bp
		mov	bp,DGROUP		; set ds --> DGROUP
		mov	ds,bp
		mov word ptr [stk1sav],sp	; save caller's stack
		mov word ptr [stk1sav+2],ss
		cli
		mov	ss,bp			; set to local stack
		mov	sp,offset stk1end
		sti
		push word ptr [stk1sav+2]	; push old ss/sp
		push word ptr [stk1sav]
		call	_dispatch		; dispatch routine
		cli
		mov	sp, word ptr [stk1sav]	; retrieve original ss/sp
		mov	ss, word ptr [stk1sav+2]
		
		pop	bp			; restore regs
		pop	es			; (these can be / are 
		pop	ds			;  modified by the service
		pop	di			;  routine)
		pop	si
		pop	dx
		pop	cx
		pop	bx
		mov	cs:[here],0		; reset sem. flag
		;
		; possible return values in AX:
		;   0xffff == not us, chain
		;   0x0000 == no error return
		;   other  == error return, set carry
		;
		cmp	ax,0ffffh		; our `not us flag'
		jz	@@notus
		or	ah,ah
		jz	@@finish1
		stc
@@finish1:
		sti
		pop	ax
		ret	2
@@notus:
		pop	ax
		jmp	cs:[_oldint2f]
_ifs0		endp


;----------------------------------------------------------------------
; unsigned ifs0_GetStackUsed(void)
;  return number of bytes in local stack which were used
;  used for debugging to minimize stack. i left lot's of space
;  just in case.
;----------------------------------------------------------------------
_ifs0_GetStackUsed proc far
		mov     bx,offset stk1
		mov	cx,STK1LEN
@@ifs0_gsu:
		cmp byte ptr [bx],0
		jnz	@@ifs0_gsur
		inc	bx
		loop	@@ifs0_gsu
@@ifs0_gsur:
		sub	bx,offset stk1
		mov	ax,STK1LEN
		sub	ax,bx
		ret

_ifs0_GetStackUsed endp
		
;------------------------------------------------------------
; our user communications hook
;------------------------------------------------------------
		db	'RIFS'
_ifs0_user	proc	far
		push	ax		; save all regs
		push	bx
		push	cx
		push	dx
		push	di
		push	si
		push	ds
		push	es
		push	bp
		mov	bp,DGROUP	; init DS
		mov	ds,bp
		mov	bp,sp
		mov	ax,sp
		push	ss		; push INTREGS *
		push	ax
		call	_UserInt	; execute user interrupt
		add	sp,4		; and return
		pop	bp
		pop	es
		pop	ds
		pop	si
		pop	di
		pop	dx
		pop	cx
		pop	bx
		pop	ax
		iret
_ifs0_user	endp

IFS0_TEXT	ends
		end
		
