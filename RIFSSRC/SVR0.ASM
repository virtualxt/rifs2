;----------------------------------------------------------------------
;----------------------------------------------------------------------
;
; this is the server - specific assembly code. 
; some notes:
;   in thought i could interrupt DOS any time outside of a DOS
;   critical area (flagged through int 2a/80 2a/81 2a/82) by simply
;   swapping the SDA. surprise! i have used this technique many times
;   in the past, but it seems that if both a background and foreground
;   process are doing directory searches, they interfere with each
;   other. that's what some of the more bizarre looking checks
;   are all about.
;
;----------------------------------------------------------------------
;----------------------------------------------------------------------

SVR0_TEXT	segment	byte public 'CODE'
DGROUP	group	_DATA,_BSS
	assume	cs:SVR0_TEXT,ds:DGROUP
SVR0_TEXT	ends

_DATA	segment word public 'DATA'
d@	label	byte
d@w	label	word

STK1LEN	equ	512 			; stack while servicing any
stk1	db	STK1LEN dup (0)		; interrupt
stk1end	equ	$
stk1sav	dd	0

userintno	db	?		; communications int #

_DATA	ends

_BSS	segment word public 'BSS'
b@	label	byte
b@w	label	word
_BSS	ends

extrn   _ServerDispatch : far
extrn	_UserInt	: far
extrn   _background     : byte
extrn	_SDA		: far ptr
extrn   _SDA_DOSBUSY	: far ptr
extrn	_lcl_SDA	: far ptr
extrn	_CDS_base	: far ptr
extrn	_lcl_CDS	: far ptr
extrn	_SDA_maxsize	: word
extrn	_SDA_minsize	: word
extrn	_CDS_TotalSize	: word
extrn	__Recieve	: far

public  _svr0_init
public  _svr0_shutdown
public	_svr0_GetSemaphores
public  _svr0_SetDispatchFlag
public  _svr0_ResetDispatchFlag
public  _svr0_GetStackUsed
public	_svr0_int1b
public	_svr0_int23
public	_svr0_int24
public	_SwapDOS

SVR0_TEXT	segment	byte public 'CODE'

;
; these are easiest to keep in the code segment. bad
; practice though :)
;
semflag		db	0		; BIOS semaphore flags
doscrit		db	0		; DOS critical flag
callsvr		db	0		; call server pending

;
; swap two memory regious
;   es:di --> src 1
;   ds:si --> src 2
;   cx    =   length
; exit:
;   ax = ?
;   cx = 0
;   si/di are incremented [cx] bytes
;   all others unchanged
; notes:
;   i don't check that either di or si (or both) are at even addresses.
;   this routine slows considerably if that is not the case!
;
swap		proc	near
		pushf
		cld			; want increment on string cmds
		shr	cx,1		; cx /= 2 (holds # of words to move)
		jz	@@m1		; if zero, skip
		pushf			; save carry flag
@@ms:
		mov	bx,es:[di]	; swap 1 word at a time
		movsw
		mov	[si-2],bx
		loop	@@ms
		popf			; get carry back
@@m1:
		jnc	@@mr		; skip on no carry
		mov	ah,es:[di]	; otherwise move odd byte
		movsb
		mov	[si-1],ah
@@mr:
		popf
		ret
swap		endp

_SwapDOS	proc	far
		push	ax
		push	cx
		push	di
		push	si
		push	es
		
		push	ds			; swap SDA
		mov	cx,[_SDA_maxsize]
		les	di,[_SDA]
		lds	si,[_lcl_SDA]
		call	swap
		pop	ds

		push	ds			; swap CDS
		mov	cx,[_CDS_TotalSize]
		les	di,[_CDS_base]
		lds	si,[_lcl_CDS]
		call	swap
		pop	ds
		pop	es
		pop	si
		pop	di
		pop	cx
		pop	ax
		ret
_SwapDOS	endp

;----------------------------------------------------------------------
; test to see if the server needs to be called (or can be called)
; and do all of the necessary prep work.
;----------------------------------------------------------------------
svrhere		db	0		; local semaphore
_svr0_TestCallSvr	proc	far
		pushf			; save things as i go along...
		push	ds
		push	bp

		mov	bp,DGROUP
		mov	ds,bp
		
                cmp     DGROUP:[_background],0	; are we a background task?
                jz      @@__svr0a		; no, skip!
		
		;
		; everything from this test until we either
		; set the flag or exit must not be interrupted!
		;
		cli
		cmp	cs:[svrhere],0		; are we here?
		jnz	@@__svr0a		; yes, exit!
		
		cmp     cs:[semflag], 0		; any BIOS locks?
		jnz	@@__svr0a		; yes, exit!
		
		cmp	cs:[doscrit],0		; DOS in critical area?
		jnz	@@__svr0a		; yes, exit!
		
		push	di			; finally, check DOS busy
		push	es
		les	di,[_SDA_DOSBUSY]
		cmp byte ptr es:[di],0		; if DOS not busy, OK
		jz	@@_svr0_OK
		cmp byte ptr es:[di],1		; if DOS ct > 1, BAD
		jnz	@@_svr0_OK
		cmp	[int28here],1		; if DOS ct IS 1
						;   and we're at int 28
						;   OK!
@@_svr0_OK:		
		pop	es
		pop	di
		jz	@@__svr1		; DOS not busy, continue
@@__svr0a:	
		jmp	@@TestCallSvrR
@@__svr1:		
		mov	cs:[svrhere],1		; set semaphore
						; save stack
		mov word ptr DGROUP:[stk1sav],sp	
		mov word ptr DGROUP:[stk1sav+2],ss

		cli				; set local stack
		mov	sp, offset stk1end
		mov	bp,ds
		mov	ss,bp
		sti
		
		push	ax			; save remaining regs
		push	bx
		push	cx
		push	dx
		push	di
		push	si
		push	es

		call	__Recieve		; check for recieve
		cmp	cs:[callsvr],0		; ready to call server?
		jz	@@__svr0_rcv		; no, done!
		
		call	_SwapDOS
		call	_ServerDispatch		; dispatch command
		call	_SwapDOS
@@__svr0_rcv:
		pop	es
		pop	si
		pop	di
		pop	dx
		pop	cx
		pop	bx
		pop	ax
		
		cli
		mov	sp,word ptr DGROUP:[stk1sav]
		mov	ss,word ptr DGROUP:[stk1sav+2]
		sti
		mov	cs:[svrhere],0		; reset semaphore
@@TestCallSvrR:
		pop	bp
		pop	ds
		popf
		ret
_svr0_TestCallSvr endp

;----------------------------------------------------------------------
; my int 9 control-break handler (ignore)
;----------------------------------------------------------------------
_svr0_int1b	proc	far
		iret
_svr0_int1b	endp

;----------------------------------------------------------------------
; here is my control-break handler (ignore)
;----------------------------------------------------------------------
_svr0_int23	proc	far
		iret
_svr0_int23	endp

;----------------------------------------------------------------------
; my critical error handler (always return FAIL)
;----------------------------------------------------------------------
_svr0_int24	proc	far
		mov	al,03h
		iret
_svr0_int24	endp

;----------------------------------------------------------------------
; clock int
;   we could only be here if the server was called during the last
;   timer tick, but better safe than sorry.
;----------------------------------------------------------------------
int08here	db	0
oldint08	dd	0
semint08	proc	far
		pushf				; emulate an INT 08
		call	cs:[oldint08]
		cli
		cmp	cs:[int08here],0	; check semaphore
		jnz	@@semint08R		; skip if here
		inc	cs:[int08here]
		call	_svr0_TestCallSvr	; try me
		dec	cs:[int08here]
@@semint08R:		
		iret				; finished
semint08	endp

;----------------------------------------------------------------------
; don't interrupt a disk action
;----------------------------------------------------------------------
oldint13	dd	0
semint13	proc	far
		inc	cs:[semflag]
		pushf
		call 	cs:[oldint13]
		pushf
		dec	cs:[semflag]
		jnz	@@__int13a
		call	_svr0_TestCallSvr
@@__int13a:		
		popf
		retf	2			; pop old flags
semint13	endp

;----------------------------------------------------------------------
; forward call calls to DOS, then try calling test server
; this assures that if a call to the server could not be put through
;   due to DOS busy, it will be processed immediatly
;----------------------------------------------------------------------
oldint21	dd	0
semint21	proc	far
		call	_svr0_TestCallSvr
		jmp	cs:[oldint21]
semint21	endp
		
;----------------------------------------------------------------------
; if in INT 0x28, we can interrupt DOS if 0 <= SDA_DOSBUSY  <= 1
;----------------------------------------------------------------------
int28here	db	0
oldint28	dd	0
semint28	proc	far
		inc	cs:[int28here]
		pushf
		call	cs:[oldint28]
		call	_svr0_TestCallSvr
		dec	cs:[int28here]
		iret
semint28	endp
		
;----------------------------------------------------------------------
; dos set critical interrupt
;----------------------------------------------------------------------
oldint2a	dd	0
semint2a	proc	far
		pushf
		cmp	ah,80h		; entering DOS critical area?
		jnz	@@sem1		; no, skip
		inc	cs:[doscrit]	; else inc. counter
		jmp     @@semrr
@@sem1:		cmp	ah,81h		; leaving DOS critical area?
		jz	@@sem2
		cmp	ah,82h
		jnz	@@semrr		; no, skip
@@sem2:		
		cmp	cs:[doscrit],0	; counter at 0?
		jz	@@semrdr	; yes, no action
		dec	cs:[doscrit]	; else decrmentS
		jnz	@@semrdr	; if not zero, skip
		call	_svr0_TestCallSvr	; else test server
@@semrdr:
@@semrr:
		popf
		jmp	cs:[oldint2a]	; chain
semint2a	endp

;----------------------------------------------------------------------
; void far svr0_init(unsigned itr)
;   initialize the various interrupts used in this section
;----------------------------------------------------------------------
_svr0_init	proc	far
		push	bp
		mov	bp,sp
		mov	ax,[bp+6]
		mov	[userintno],al

		mov	ah,35h				; get user int
		int	21h
		mov word ptr cs:[olduser], bx
		mov word ptr cs:[olduser+2], es

                cmp     [_background],0		; if this is not to run in the
                jz      @@svr0_init0		; background, we're done!
                
		mov	al,[userintno]
		mov	ah,25h
		push	ds
		push	cs
		pop	ds
		mov	dx,offset _svr0_user
		int	21h
		pop	ds

		push	ds
		push	cs
		pop	ds
		assume	ds:SVR0_TEXT
		
		;
		; grab timer int
		;
		mov	ax,3508h
		int	21h
		mov word ptr [oldint08],bx
		mov word ptr [oldint08+2],es
		mov	dx,offset semint08
		mov	ax,2508h
		int	21h
		
		;
		; grab disk i/o int
		;
		mov	ax,3513h
		int	21h
		mov word ptr [oldint13],bx
		mov word ptr [oldint13+2],es
		mov	dx,offset semint13
		mov	ax,2513h
		int	21h

		;
		; grab DOS call
		;
		mov	ax,3521h
		int	21h
		mov word ptr [oldint21],bx
		mov word ptr [oldint21+2],es
		mov	dx,offset semint21
		mov	ax,2521h
		int	21h

		;
		; grab DOS waiting
		;
		mov	ax,3528h
		int	21h
		mov word ptr [oldint28],bx
		mov word ptr [oldint28+2],es
		mov	dx,offset semint28
		mov	ax,2528h
		int	21h
		
		;
		; grab dos critical int
		;
		mov	ax,352ah
		int	21h
		mov word ptr [oldint2a],bx
		mov word ptr [oldint2a+2],es
		mov	dx,offset semint2a
		mov	ax,252ah
		int	21h

		pop	ds
@@svr0_init0:
		pop	bp
		ret
_svr0_init	endp

;----------------------------------------------------------------------
; void far svr0_shutdown(void)
;   reset everything to startup values. no checking is done
;   so if this is called before svr0_initialize the machine will
;   go balistic!
;----------------------------------------------------------------------
_svr0_shutdown	proc	far
		assume  ds:DGROUP
                cmp     [_background],0
                jz      @@svr0_shut0
                
		mov	al,[userintno]
		push	ds

		lds	dx,cs:[olduser]
		mov	ah,25h
		int	21h

                pop     ds

                push    ds
                lds	dx,cs:[oldint08]
                mov	ax,2508h
                int	21h
                
		lds	dx,cs:[oldint13]
		mov	ax,2513h
		int	21h

		lds	dx,cs:[oldint21]
		mov	ax,2521h
		int	21h

		lds	dx,cs:[oldint28]
		mov	ax,2528h
		int	21h
		
		lds	dx,cs:[oldint2a]
		mov	ax,252ah
		int	21h
		pop	ds
@@svr0_shut0:
		ret
_svr0_shutdown	endp

;----------------------------------------------------------------------
; unsigned far svr0_GetSemaphores(void)
;   return the semaphore values
;      MSB = dos critical flag
;      LSB = bios critical flag
;----------------------------------------------------------------------
_svr0_GetSemaphores proc far
		mov	ax,word ptr cs:[semflag]
		ret
_svr0_GetSemaphores endp

;----------------------------------------------------------------------
; void far svr0_SetDispatchFlag(void)
;   set the Dispatch flag to TRUE, so the next srv0_TestCallSvr
;   will trigger a dispatch function
;----------------------------------------------------------------------
_svr0_SetDispatchFlag proc far
		mov	cs:[callsvr],1
		ret
_svr0_SetDispatchFlag endp

;----------------------------------------------------------------------
; void far svr0_ResetDispatchFlag(void)
;   reset the Dispatch flag
;----------------------------------------------------------------------
_svr0_ResetDispatchFlag proc far
		mov	cs:[callsvr],0
		ret
_svr0_ResetDispatchFlag endp


;----------------------------------------------------------------------
; unsigned far svr0_GetStackUsed(void)
;   return the maximum number of local stack bytes used. this was
;   mostly for debugging.
;----------------------------------------------------------------------
_svr0_GetStackUsed proc far
		push	di
		mov     di,offset stk1
		mov	cx,STK1LEN
		push	ds
		pop	es
		mov	al,0
		cld
repz		scasb		
		sub	di,offset stk1
		mov	ax,STK1LEN
		sub	ax,di
		pop	di
		ret
_svr0_GetStackUsed endp

;----------------------------------------------------------------------
; this is how we communicate. 
; it is user selectable. the RIFS at the beginning is how we
; find ourselves.
;----------------------------------------------------------------------
olduser		dd	0
		db	'RIFS'
_svr0_user	proc	far
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
		mov	cs:[svrhere],1
		push	ss		; push INTREGS *
		push	bp
		call	_UserInt	; execute user interrupt
		add	sp,4		; and return
		mov	cs:[svrhere],0
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
_svr0_user	endp
SVR0_TEXT	ends
		end
		
