;----------------------------------------------------------------------
;----------------------------------------------------------------------
;
; this is coded in assembly for speed. roughly 25K/sec on an 8088
; from the comp.compression FAQ
;
;----------------------------------------------------------------------
;----------------------------------------------------------------------
CRC_TEXT        segment byte public 'CODE'
DGROUP  group   _DATA,_BSS
        assume  cs:CRC_TEXT,ds:DGROUP
CRC_TEXT        ends
_DATA   segment word public 'DATA'
d@      label   byte
d@w     label   word


_crc32_table dd	000000000h, 004c11db7h, 009823b6eh, 00d4326d9h
	dd	0130476dch, 017c56b6bh, 01a864db2h, 01e475005h
	dd	02608edb8h, 022c9f00fh, 02f8ad6d6h, 02b4bcb61h
	dd	0350c9b64h, 031cd86d3h, 03c8ea00ah, 0384fbdbdh
	dd	04c11db70h, 048d0c6c7h, 04593e01eh, 04152fda9h
	dd	05f15adach, 05bd4b01bh, 0569796c2h, 052568b75h
	dd	06a1936c8h, 06ed82b7fh, 0639b0da6h, 0675a1011h
	dd	0791d4014h, 07ddc5da3h, 0709f7b7ah, 0745e66cdh
	dd	09823b6e0h, 09ce2ab57h, 091a18d8eh, 095609039h
	dd	08b27c03ch, 08fe6dd8bh, 082a5fb52h, 08664e6e5h
	dd	0be2b5b58h, 0baea46efh, 0b7a96036h, 0b3687d81h
	dd	0ad2f2d84h, 0a9ee3033h, 0a4ad16eah, 0a06c0b5dh
	dd	0d4326d90h, 0d0f37027h, 0ddb056feh, 0d9714b49h
	dd	0c7361b4ch, 0c3f706fbh, 0ceb42022h, 0ca753d95h
	dd	0f23a8028h, 0f6fb9d9fh, 0fbb8bb46h, 0ff79a6f1h
	dd	0e13ef6f4h, 0e5ffeb43h, 0e8bccd9ah, 0ec7dd02dh
	dd	034867077h, 030476dc0h, 03d044b19h, 039c556aeh
	dd	0278206abh, 023431b1ch, 02e003dc5h, 02ac12072h
	dd	0128e9dcfh, 0164f8078h, 01b0ca6a1h, 01fcdbb16h
	dd	0018aeb13h, 0054bf6a4h, 00808d07dh, 00cc9cdcah
	dd	07897ab07h, 07c56b6b0h, 071159069h, 075d48ddeh
	dd	06b93dddbh, 06f52c06ch, 06211e6b5h, 066d0fb02h
	dd	05e9f46bfh, 05a5e5b08h, 0571d7dd1h, 053dc6066h
	dd	04d9b3063h, 0495a2dd4h, 044190b0dh, 040d816bah
	dd	0aca5c697h, 0a864db20h, 0a527fdf9h, 0a1e6e04eh
	dd	0bfa1b04bh, 0bb60adfch, 0b6238b25h, 0b2e29692h
	dd	08aad2b2fh, 08e6c3698h, 0832f1041h, 087ee0df6h
	dd	099a95df3h, 09d684044h, 0902b669dh, 094ea7b2ah
	dd	0e0b41de7h, 0e4750050h, 0e9362689h, 0edf73b3eh
	dd	0f3b06b3bh, 0f771768ch, 0fa325055h, 0fef34de2h
	dd	0c6bcf05fh, 0c27dede8h, 0cf3ecb31h, 0cbffd686h
	dd	0d5b88683h, 0d1799b34h, 0dc3abdedh, 0d8fba05ah
	dd	0690ce0eeh, 06dcdfd59h, 0608edb80h, 0644fc637h
	dd	07a089632h, 07ec98b85h, 0738aad5ch, 0774bb0ebh
	dd	04f040d56h, 04bc510e1h, 046863638h, 042472b8fh
	dd	05c007b8ah, 058c1663dh, 0558240e4h, 051435d53h
	dd	0251d3b9eh, 021dc2629h, 02c9f00f0h, 0285e1d47h
	dd	036194d42h, 032d850f5h, 03f9b762ch, 03b5a6b9bh
	dd	00315d626h, 007d4cb91h, 00a97ed48h, 00e56f0ffh
	dd	01011a0fah, 014d0bd4dh, 019939b94h, 01d528623h
	dd	0f12f560eh, 0f5ee4bb9h, 0f8ad6d60h, 0fc6c70d7h
	dd	0e22b20d2h, 0e6ea3d65h, 0eba91bbch, 0ef68060bh
	dd	0d727bbb6h, 0d3e6a601h, 0dea580d8h, 0da649d6fh
	dd	0c423cd6ah, 0c0e2d0ddh, 0cda1f604h, 0c960ebb3h
	dd	0bd3e8d7eh, 0b9ff90c9h, 0b4bcb610h, 0b07daba7h
	dd	0ae3afba2h, 0aafbe615h, 0a7b8c0cch, 0a379dd7bh
	dd	09b3660c6h, 09ff77d71h, 092b45ba8h, 09675461fh
	dd	08832161ah, 08cf30badh, 081b02d74h, 0857130c3h
	dd	05d8a9099h, 0594b8d2eh, 05408abf7h, 050c9b640h
	dd	04e8ee645h, 04a4ffbf2h, 0470cdd2bh, 043cdc09ch
	dd	07b827d21h, 07f436096h, 07200464fh, 076c15bf8h
	dd	068860bfdh, 06c47164ah, 061043093h, 065c52d24h
	dd	0119b4be9h, 0155a565eh, 018197087h, 01cd86d30h
	dd	0029f3d35h, 0065e2082h, 00b1d065bh, 00fdc1bech
	dd	03793a651h, 03352bbe6h, 03e119d3fh, 03ad08088h
	dd	02497d08dh, 02056cd3ah, 02d15ebe3h, 029d4f654h
	dd	0c5a92679h, 0c1683bceh, 0cc2b1d17h, 0c8ea00a0h
	dd	0d6ad50a5h, 0d26c4d12h, 0df2f6bcbh, 0dbee767ch
	dd	0e3a1cbc1h, 0e760d676h, 0ea23f0afh, 0eee2ed18h
	dd	0f0a5bd1dh, 0f464a0aah, 0f9278673h, 0fde69bc4h
	dd	089b8fd09h, 08d79e0beh, 0803ac667h, 084fbdbd0h
	dd	09abc8bd5h, 09e7d9662h, 0933eb0bbh, 097ffad0ch
	dd	0afb010b1h, 0ab710d06h, 0a6322bdfh, 0a2f33668h
	dd	0bcb4666dh, 0b8757bdah, 0b5365d03h, 0b1f740b4h

_DATA   ends
_BSS    segment word public 'BSS'
b@      label   byte
b@w     label   word
_BSS    ends

CRC_TEXT        segment byte public 'CODE'

public  _crc32

;
; DWORD crc32(DWORD crc, void *buf, WORD length)
;   initial crc = 0xffffffff 
;
_crc32  proc    far
        push    bp
        mov     bp,sp
        push	di
        push	si

	mov	di,[bp+ 6]	; dx:di = CRC accum
	mov	dx,[bp+ 8]
	not	di
	not	dx
	les	si,[bp+10]	; es:si --> buffer
	mov	cx,[bp+14]	; cx = length
	cld
@@top:
	mov	bl,dh		; bx = dx:di >> 24
	mov	bh,0
	db	26h		; ES: (segment override)
	lodsb			; al = es:[si]; si++
	xor	bl,al		; bx ^= al
	add	bx,bx		; 
	add	bx,bx
	add	bx, offset _crc32_table
	mov	ax,di
	mov	dh,dl
	mov	dl,ah
	mov	ah,al
	mov	al,0
	xor	ax,[bx]
	xor	dx,[bx+2]
	mov	di,ax
	loop	@@top
	mov	ax,di
	not	ax
	not	dx
        pop	si
        pop	di
        mov     sp,bp
        pop     bp
        ret     
_crc32  endp

CRC_TEXT        ends
        end
