;----------------------------------------------------------------------
;----------------------------------------------------------------------
;
; this is the assembly stub for COMIO. all of COMIO should
; probably be written in assembly for speed, but i've run
; into no speed problem having it in C
;
;----------------------------------------------------------------------
;----------------------------------------------------------------------

COM0_TEXT	segment	byte public 'CODE'
DGROUP	group	_DATA,_BSS
	assume	cs:COM0_TEXT,ds:DGROUP
COM0_TEXT	ends

_DATA	segment word public 'DATA'
d@	label	byte
d@w	label	word

_DATA	ends

_BSS	segment word public 'BSS'
b@	label	byte
b@w	label	word
_BSS	ends

extrn	_LSR:word
extrn	_MSR:word
extrn	_TRB:word
extrn	_IIR:word
extrn	_CommIO:far

public  _com0_init
public  _com0_shutdown
public	_com0

COM0_TEXT	segment	byte public 'CODE'

;----------------------------------------------------------------------
; initialize this stub. grabs/sets int 60H and the serial interrupt
; passed on the stack
;----------------------------------------------------------------------
_oldint		dd	?	; this points to the old INT 60 code
_com0_init	proc	far
		push	bp
		mov	bp,sp
		mov	ah,35h
		int	21h
		mov word ptr cs:[_oldint], bx
		mov word ptr cs:[_oldint+2], es

		pop	bp
		ret
_com0_init	endp

;----------------------------------------------------------------------
; restore the interrupts
;----------------------------------------------------------------------
_com0_shutdown proc	far
		ret
_com0_shutdown endp

;----------------------------------------------------------------------
; reset the PIC, save all registers, and call our I/O routine
; the [here] semaphore is probably unnecessary, but better safe
; than sorry
;----------------------------------------------------------------------
here		db	0
_com0	   	proc far
		push	ax
		mov	al,20h			; re-enable PIC
		out	20h, al
		cmp	cs:[here],0		; test semaphore
		jnz	@@skip			; skip if already here
		mov	cs:[here],1		; set semaphore
		
		push	ds
		mov	ax,DGROUP
		mov	ds,ax
		
		push	bx
		push	cx
		push	dx
		push	si
		push	di
		push	es
		push	bp
		;
		; for some reason at higher speeds on slow
		; machines, the machine will lock if i re-enable
		; interrupts here.
		;
		;sti
		call	_CommIO			; dispatch routine
		;cli                            ; no interrupts while
                                                ; resetting the stack
		pop	bp			; restore all registers
		pop	es
		pop	di
		pop	si
		pop	dx
		pop	cx
		pop	bx
		pop	ds
		mov	cs:[here],0		; reset semaphore
@@skip:
		pop	ax
		iret
_com0		endp
COM0_TEXT	ends
		end
		
