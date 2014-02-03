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
; Bootloader data
boot_msg db "ListFS bootloader",13,10,0
reboot_msg db "Press any key for restart...",13,10,0
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
	; Show message
	mov si, boot_msg
	call write_msg
	; Reboot
	jmp reboot
; Free space and signature
rb 510 - ($ - $$)
db 0x55, 0xAA