org 0x9000
	dw 0x7C00 ; magic
	entry_point dw start
	reboot dw ?
	error dw ?
	write_msg dw ?
	load_sector dw ?
	load_file dw ?
	disk_id dw ?
	low_memory_size dw ?
; Data
start_msg db "Secondary boot loader started",13,10,13,10,0
digits db "0123456789ABCDEF"
num_buffer db "0x0000",0
new_line db 13,10,0
; Convert number from AX to string num_buffer
num_to_str:
	push ax bx cx di
	mov di, num_buffer + 5
	mov cx, 4
	std
@@:
	mov bx, ax
	and bx, 0x000F
	add bx, digits
	push ax
	mov al, [bx]
	stosb
	pop ax
	shr ax, 4
	loop @b
	cld
	pop di cx bx ax
	ret
; Display value with label
macro write_value lbl,value {
	local .text
	jmp @f
	.text db lbl," = ",0
@@:
	push ax si
	mov si, .text
	call [write_msg]
	mov ax, value
	call num_to_str
	mov si, num_buffer
	call [write_msg]
	mov si, new_line
	call [write_msg]
	pop si ax
}
; Entry point
start:
	mov si, start_msg
	call [write_msg]
	write_value "stack_pointer",sp
	write_value "reboot",[reboot]
	write_value "error",[error]
	write_value "write_msg",[write_msg]
	write_value "load_sector",[load_sector]
	write_value "load_file",[load_file]
	write_value "disk_id",[disk_id]
	write_value "low_memory_size",[low_memory_size]
	jmp [reboot]