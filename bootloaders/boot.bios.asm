; ListFS bootloader
org 0x7C00 
	jmp boot
; ListFS header
align 4
fs_magic dd ?
fs_base dq ?
fs_size dq ?
fs_map_base dq ?
fs_map_size dq ?
fs_first_file dq ?
fs_block_size dw ?
fs_version dw ?
fs_used_blocks dq ?
; Bootloader data
boot_msg db "Loading ListFS bootloader...",0
reboot_msg db 13,10,"Press any key for restart...",13,10,0
label disk_id byte at $$ + 4
disk_heads dw 0
label disk_spt word at $$
extra_boot_loader_sector dq 1
virtual at 0x600
dap:
	.size db ?
	.reserved db ?
	.sector_count dw ?
	.offset dw ?
	.segment dw ?
	.sector dq ?
end virtual
label low_memory_size word at $$ + 2
; Write message from DS:SI
write_msg:
	push ax si
	mov ah, 0x0E
@@:
	lodsb
	test al, al
	jz @f
	int 0x10
	jmp @b
@@:
	pop si ax
	ret
; Load sector DS:SI to buffer BX:0
load_sector:
	push ax bx cx dx si di es
	mov di, dap.sector
	mov cx, 8 / 2
	rep movsw
	mov ax, word[fs_base]
	add word[dap.sector], ax
	mov ax, word[fs_base + 2]
	adc word[dap.sector + 2], ax
	mov ax, word[fs_base + 4]
	adc word[dap.sector + 4], ax
	mov ax, word[fs_base + 6]
	adc word[dap.sector + 6], ax
	cmp [disk_heads], 0
	jne .load_sector_chs
; Load sector using LBA
.load_sector_lba:
	mov [dap.size], 16
	mov [dap.reserved], 0
	mov [dap.sector_count], 1
	mov [dap.segment], bx
	mov [dap.offset], 0
	mov si, dap
	mov dl, [disk_id]
	mov ah, 0x42
	int 0x13
	jc .error
	jmp .exit
; Load sector using CHS
.load_sector_chs:
	mov ax, word[dap.sector]
	mov dx, word[dap.sector + 2]
	div [disk_spt]
	mov cl, dl
	inc cl
	xor dx, dx
	div [disk_heads]
	mov dh, dl
	mov ch, al
	shl ah, 6
	or cl, ah
	mov dl, [disk_id]
	mov es, bx
	xor bx, bx
	mov si, 5
	mov al, 1
@@:
	mov ah, 2
	int 0x13
	jnc .exit
	xor ah, ah
	int 0x13
	dec si
	jnz @b
.error:
	call error
	db "DISK ERROR",0
.exit:
	pop es di si dx cx bx ax
	ret
; Out of memory
out_of_memory:
	call error
	db "OUT OF MEMORY",0
; File not found
not_found:
	call error
	db "NOT FOUND",0
; Error
error:
	pop si
	call write_msg
; Reboot
reboot:
	mov si, reboot_msg
	call write_msg
	xor ah, ah
	int 0x16
	jmp 0xFFFF:0
; Bootloader entry point
boot:
	; Setup segment registers and stack
	jmp 0:@f
@@:
	mov ax, cs
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, $$
	; Enable interrupts
	sti
	; Detect disk
	mov [disk_id], dl
	mov ah, 0x41
	mov bx, 0x55AA
	int 0x13
	jnc @f
	mov ah, 8
	xor di, di
	push es
	int 0x13
	pop es
 	jc load_sector.error
	inc dh
	shr dx, 8
	mov [disk_heads], dx
	and cx, 111111b
	mov [disk_spt], cx
@@:
	; Detect low memory size
	xor ax, ax
	int 0x12
	jc out_of_memory
	test ax, ax
	jz out_of_memory
	shl ax, 10 - 4
	mov [low_memory_size], ax
	; Display welcome message
	mov si, boot_msg
	call write_msg
	; Load addional bootloader code
	mov si, extra_boot_loader_sector
	mov bx, extra_boot_code_base shr 4
	xor cx, cx
	xor dx, dx
@@:
	cmp bx, [low_memory_size]
	jae out_of_memory
	call load_sector
	cmp word[extra_boot_sig], 0x55AA
	jne not_found
	add word[si], 1
	adc word[si + 2], dx
	adc word[si + 4], dx
	adc word[si + 6], dx
	add bx, 512 shr 4
	inc cx
	cmp cx, [extra_boot_code_size]
	jb @b
	; Display okay message
	mov si, ok_msg
	call write_msg
	; Jump to addional bootloader
	jmp extra_boot_entry
; Free space and signature
rb 510 - ($ - $$)
db 0x55, 0xAA
; Addional bootloader code
extra_boot_code_base:
; Addional bootloader data
extra_boot_code_size dw 1
ok_msg db "OK",13,10,0
; Addional bootloader entry point
extra_boot_entry:
	; Reboot
	jmp reboot
; Free space and signature
rb 1022 - ($ - $$)
extra_boot_sig db 0xAA, 0x55