#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <Windows.h>
#include <conio.h>

HANDLE hStdin = INVALID_HANDLE_VALUE;

//2^16 Memory Locations (65536)
uint16_t memory[UINT16_MAX];

/*
8 General Purpose Registers (R0 - R7)
1 Program Counter Register (PC)
1 Condition Flag Register (COND)
*/
enum {
	R_R0 = 0,
	R_R1,
	R_R2,
	R_R3,
	R_R4,
	R_R5,
	R_R6,
	R_R7,
	R_PC,   //Program Counter
	R_COND, //Condition Code Register
	R_COUNT
};

//Register Storage
uint16_t reg[R_COUNT];

//Opcodes for the Instructions 
enum {
	OP_BR = 0, //Branch
	OP_ADD,    //Add 
	OP_LD,     //Load
	OP_ST,     //Store
	OP_JSR,    //Jump Register
	OP_AND,    //And
	OP_LDR,    //Load Register
	OP_STR,    //Store Register
	OP_RTI,    //unsed
	OP_NOT,    //Not
	OP_LDI,    //Load Indirect
	OP_STI,    //Store Indirect
	OP_JMP,    //Jump
	OP_RES,    //Reserved (unused)
	OP_LEA,    //Load Effective Address
	OP_TRAP    //Execute Trap
};

//Condition Flags 
/*
Note : << is the shift left operator 
n << k shifts the bits of n to the left k places
*/
enum {
	FL_POS = 1 << 0,  //Positive
	FL_ZRO = 1 << 1,  //Zero
	FL_NEG = 1 << 2,  //Negative
};

//Trap Codes 
enum {
	TRAP_GETC = 0x20,   //Get character from keyboard, not echoed onto terminal
	TRAP_OUT = 0x21,    //Output a character
	TRAP_PUTS = 0x22,   //Output a word string
	TRAP_IN = 0x23,     //Get character from keyboard, echoed onto terminal
	TRAP_PUTSP = 0x24,  //Output a byte string
	TRAP_HALT = 0x25    //Halt the program
};

//Memory Mapped Registers 
enum {
	MR_KBSR = 0xFE00,  //Keyboard Status
	MR_KBDR = 0xFE02   //Keyboard Data 
};

//Operation Functions 
uint16_t sign_extend(uint16_t x, int bit_count) {
	if((x >> (bit_count - 1)) & 1) {
		x |= (0xFFFF << bit_count);
	}
	
	return x;
}

void update_flags(uint16_t r) {
	if(reg[r] == 0) {
		reg[R_COND] = FL_ZRO;
	}
	else if(reg[r] >> 15) { //A 1 in the leftmost bit indicates negative
		reg[R_COND] = FL_NEG;
	}
	else {
		reg[R_COND] = FL_POS;
	}
}

uint16_t swap16(uint16_t x) {
	return (x << 8) | (x >> 8);
}
uint16_t check_key() {
	return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

void read_image_file(FILE* file) {
	//Where in memory to place image
	uint16_t origin;
	fread(&origin, sizeof(origin), 1, file);
	origin = swap16(origin);
	
	uint16_t max_read = UINT16_MAX - origin;
	uint16_t* p = memory + origin;
	size_t read = fread(p, sizeof(uint16_t), max_read, file);
	
	//Swap to little endian
	while(read-- > 0) {
		*p = swap16(*p);
		++p;
	}
}

int read_image(const char* image_path) {
	FILE* file = fopen(image_path, "rb");
	if(!file) {return 0;};
	read_image_file(file);
	fclose(file);
	return 1;
}

//Memory Access Functions 
void mem_write(uint16_t address, uint16_t val) {
	memory[address] = val;
}
uint16_t mem_read(uint16_t address) {
	if(address == MR_KBSR) {
		if(check_key()) {
			memory[MR_KBSR] = (1 << 15);
			memory[MR_KBDR] = getchar();
		}
		else {
			memory[MR_KBSR] = 0;
		}
	}
	
	return memory[address];
}

DWORD fdwMode, fdwOldMode;
void disable_input_buffering() {
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(hStdin, &fdwOldMode); //Save old mode
	fdwMode = fdwOldMode 
			^ ENABLE_ECHO_INPUT  //No input echo
			^ ENABLE_LINE_INPUT; //Return when one or more characters are availible
			
	SetConsoleMode(hStdin, fdwMode); //Set New Mode
	FlushConsoleInputBuffer(hStdin); //Clear Buffer
}
void restore_input_buffering() {
	SetConsoleMode(hStdin, fdwOldMode);
}

/* Handle Interrupt */
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

int main(int argc, const char* argv[]) {
	
	if(argc < 2) {
		printf("LC-3 [image-file1] ... \n");
		exit(2);	
	}
	
	for(int j = 1; j < argc; ++j) {
		if(!read_image(argv[j])){
			printf("Failed to load image: %s\n", argv[j]);
			exit(1);
		}
	}
	
	signal(SIGINT, handle_interrupt);
	disable_input_buffering();
	
	//Set the PC to the starting position 
	//0x3000 is the default location
	enum {
		PC_START = 0x3000
	};
	reg[R_PC] = PC_START;
	
	int running = 1;
	while (running){
		//Fetch 
		uint16_t instr = mem_read(reg[R_PC]++);
		uint16_t op = instr >> 12;
		
		switch(op) {
			//Availible Opcodes 
			case OP_ADD:
				{
					uint16_t r0 = (instr >> 9) & 0x7;        //Destination Register
					uint16_t r1 = (instr >> 6) & 0x7;        //First operand SR1
					uint16_t imm_flag = (instr >> 5) & 0x1;  //Whether or not in immediate mode 
					
					//If immediate 
					if(imm_flag) {
						uint16_t imm5 = sign_extend(instr & 0x1F, 5);
						reg[r0] = reg[r1] + imm5;
					}
					//If not in immediate
					else {
						uint16_t r2 = instr & 0x7;
						reg[r0] = reg[r1] + reg[r2];
					}
					
					update_flags(r0); //Update the flags 
				}
				break;
			case OP_AND:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t r1 = (instr >> 6) & 0x7;
					uint16_t imm_flag = (instr >> 5) & 0x1;
					
					//If in immediate
					if(imm_flag) {
						uint16_t imm5 = sign_extend(instr & 0x1F, 5);
						reg[r0] = reg[r1] & imm5;
					}
					else {
						uint16_t r2 = instr & 0x7;
						reg[r0] = reg[r1] & reg[r2];
					}
					
					update_flags(r0);
				}
				break;
			case OP_NOT:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t r1 = (instr >> 6) & 0x7;
					
					reg[r0] = ~reg[r1];
					
					update_flags(r0);
				}
				break;
			case OP_BR:
				{
					uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
					uint16_t cond_flag = (instr >> 9) & 0x7;
					
					if(cond_flag & reg[R_COND]) {
						reg[R_PC] += pc_offset;
					}
				}
				break;
			case OP_JMP:
				{
					uint16_t r1 = (instr >> 6) & 0x7;
					reg[R_PC] = reg[r1];
				}
				break;
			case OP_JSR:
				{
					uint16_t long_flag = (instr >> 11) & 1;
					reg[R_R7] = reg[R_PC];
					
					if(long_flag) {
						uint16_t long_pc_offset = sign_extend(instr & 0x7FF, 11);
						reg[R_PC] += long_pc_offset; //JSR
					}
					else {
						uint16_t r1 = (instr >> 6) & 0x7;
						reg[R_PC] = reg[r1]; //JSRR
					}
					break;
				}
				break;
			case OP_LD:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
					
					reg[r0] = mem_read(reg[R_PC] + pc_offset);
					
					update_flags(r0);
				}
				break;
			case OP_LDI:
				{
					uint16_t r0 = (instr >> 9) & 0x7;  //Destination Register 
					uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);  //PC Offset
					
					//Add the offset to the current PC
					reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
					
					update_flags(r0); //Update the flags
				}
				break;
			case OP_LDR:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t r1 = (instr >> 6) & 0x7;
					uint16_t offset = sign_extend(instr & 0x3F, 6);
					
					reg[r0] = mem_read(reg[r1] + offset);
					
					update_flags(r0);
				}
				break;
			case OP_LEA:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
					
					reg[r0] = reg[R_PC] + pc_offset;
					
					update_flags(r0);
				}
				break;
			case OP_ST:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
					
					mem_write(reg[R_PC] + pc_offset, reg[r0]);
				}
				break;
			case OP_STI:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
					
					mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
				}
				break;
			case OP_STR:
				{
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t r1 = (instr >> 6) & 0x7;
					uint16_t offset = sign_extend(instr & 0x3F, 6);
					
					mem_write(reg[r1] + offset, reg[r0]);
				}
				break;
			case OP_TRAP:
				switch(instr & 0xFF) {
					case TRAP_GETC:
						//Read a single ASCII char
						reg[R_R0] = (uint16_t)getchar();
						break;
					case TRAP_OUT:
						putc((char)reg[R_R0], stdout);
						fflush(stdout);
						break;
					case TRAP_PUTS:
						{
							//One Character per word 
							uint16_t* c = memory + reg[R_R0];
							
							while(*c) {
								putc((char)*c, stdout);
								++c;
							}
							
							fflush(stdout);
						}
						break;
					case TRAP_IN:
						{
							printf("Enter a character: ");
							char c = getchar();
							putc(c, stdout);
							reg[R_R0] = (uint16_t)c;
						}
						break;
					case TRAP_PUTSP:
						{
							/*
							one char ber byte (two bytes per word)
							swap back to Big Endian format
							*/
							uint16_t* c = memory + reg[R_R0];
							
							while(*c) {
								char char1 = (*c) & 0xFF;
								putc(char1, stdout);
								char char2 = (*c) >> 8;
								if (char2) putc(char2, stdout);
								++c;
							}
							
							fflush(stdout);
						}
						break;
					case TRAP_HALT:
						puts("HALT");
						fflush(stdout);
						running = 0;
						break;
				}
				break;
			case OP_RES:
			case OP_RTI:
			default:
				//Bad Opcode
				abort();
				break;
		}
	}
	
	restore_input_buffering();
	
	return 0;
}
