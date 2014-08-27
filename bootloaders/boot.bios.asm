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
extra_boot_loader_sector dq 1
boot_msg db "Loading ListFS bootloader...",0
reboot_msg db 13,10,"Press any key for restart...",13,10,0
label disk_id byte at $$ + 4
disk_heads dw 0
label disk_spt word at $$
virtual at 0x600
dap:
	.size db ?
	.reserved db ?
	.sector_count dw ?
	.offset dw ?
	.segment dw ?
	.sector dq ?
end virtual
virtual at 0x1000
f_info:
	.name db 256 dup (?)
	.parent dq ?
	.next dq ?
	.prev dq ?
	.data dq ?
	.magic dd ?
	.flags dd ?
	.size dd ?
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
load_msg_prefix db 'Loading "',0
load_msg_suffix db '"...',0
secondary_boot_file_name db "boot.bin",0
load_block_tmp_index dq ?
virtual at 0x9000
secondary_boot:
	.magic dw ?
	.entry_point dw ?
	.reboot dw ?
	.error dw ?
	.write_msg dw ?
	.load_sector dw ?
	.load_file dw ?
	.disk_id dw ?
	.low_memory_size dw ?
end virtual
; Load ListFS block with index DS:SI to buffer BX:0 (BX will be pointer to end of file data)
load_block:
	push ax bx cx si di
	mov di, load_block_tmp_index
	mov cx, 8 / 2
	rep movsw
	mov si, load_block_tmp_index
	xor cx, cx
@@:
	call load_sector
	xor ax, ax
	add word[load_block_tmp_index], 1
	adc word[load_block_tmp_index + 2], ax
	adc word[load_block_tmp_index + 4], ax
	adc word[load_block_tmp_index + 6], ax
	add bx, 512 shr 4
	add cx, 512
	cmp cx, [fs_block_size]
	jb @b
	pop di si cx bx ax
	ret
; Put DS:SI string length to CX
strlen:
	push ax di
	mov cx, 0xFFFF
	xor al, al
	repne scasb
	neg cx
	dec cx
	pop di ax
	ret
; Split file name DS:BP
split_file_name:
	push ax
	mov si, bp
@@:
	lodsb
	test al, al
	jz @f
	cmp al, '/'
	je @f
	jmp @b
@@:
	cmp byte[si - 1], 0
	jne @f
	xor si, si
	jmp .exit
@@:
	mov byte[si - 1], 0
.exit:
	pop ax
	ret
; Load file with name DS:SI to buffer BX:0 (Result: BX pointers to end of file data)
load_file:
	push ax cx si di bp bx
	mov bp, si
	mov si, load_msg_prefix
	call write_msg
	mov si, bp
	call write_msg
	mov si, load_msg_suffix
	call write_msg
	call split_file_name
	push si
	mov bx, f_info shr 4
	mov si, fs_first_file
.search:
	mov ax, word[si]
	and ax, word[si + 2]
	and ax, word[si + 4]
	and ax, word[si + 6]
	cmp ax, 0xFFFF
	je not_found
	call load_block
	push si
	mov si, f_info.name
	mov di, bp
	call strlen
	repe cmpsb
	pop si
	je .found
	mov si, f_info.next
	jmp .search
.found:
	pop si
	test si, si
	jnz @f
	test [f_info.flags], 1
	jz .load_data
	jmp not_found
@@:
	test [f_info.flags], 1
	jz not_found
	mov bp, si
	call split_file_name
	push si
	mov si, f_info.data
	jmp .search
.load_data:
	mov si, f_info.data
.load_block_list:
	mov ax, word[si]
	and ax, word[si + 2]
	and ax, word[si + 4]
	and ax, word[si + 6]
	cmp ax, 0xFFFF
	je .file_end
	mov bx, f_info shr 4
	call load_block
	mov si, f_info + 8
.load_block:
	pop bx
	call load_block
	mov ax, [fs_block_size]
	shr ax, 4
	add bx, ax
	cmp bx, [low_memory_size]
	jae out_of_memory
	push bx
	add si, 8
	mov ax, si
	add ax, 8
	cmp ax, [fs_block_size]
	jb .load_block
	jmp .load_block_list
.file_end:
	mov si, ok_msg
	call write_msg
	pop bx bp di si cx ax
	ret
; Addional bootloader entry point
extra_boot_entry:
	; Load secondary boot loader
	mov si, secondary_boot_file_name
	mov bx, secondary_boot shr 4
	call load_file
	; Check secondary boot loader magic
	cmp [secondary_boot.magic], 0x7C00
	je @f
	call error
	db "WRONG MAGIC",0
@@:
	; Fill function table
	mov [secondary_boot.reboot], reboot
	mov [secondary_boot.error], error
	mov [secondary_boot.write_msg], write_msg
	mov [secondary_boot.load_sector], load_sector
	mov [secondary_boot.load_file], load_file
	xor ax, ax
	mov al, [disk_id]
	mov [secondary_boot.disk_id], ax
	mov ax, [low_memory_size]
	mov [secondary_boot.low_memory_size], ax
	; Jump to secondary boot loader
	jmp [secondary_boot.entry_point]
; Free space and signature
rb 1022 - ($ - $$)
extra_boot_sig db 0xAA, 0x55