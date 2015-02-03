/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - dbg_memory.c                                            *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2008 DarkJeztr                                          *
 *   Copyright (C) 2002 Blight                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <string.h>

#include "dbg_types.h"
#include "dbg_memory.h"
#include "dbg_breakpoints.h"

#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "main/main.h"
#include "main/rom.h"
#include "memory/memory.h"
#include "r4300/cached_interp.h"
#include "r4300/r4300.h"
#include "r4300/ops.h"
#include "r4300/tlb.h"
#include "ri/ri_controller.h"
 
#if !defined(NO_ASM) && (defined(__i386__) || defined(__x86_64__))

#include <dis-asm.h>
#include <stdarg.h>

static int  lines_recompiled;
static uint32 addr_recompiled;
static int  num_decoded;

static char opcode_recompiled[564][MAX_DISASSEMBLY];
static char args_recompiled[564][MAX_DISASSEMBLY*4];
static void *opaddr_recompiled[564];

static disassemble_info dis_info;

#define CHECK_MEM(address) \
   if (!invalid_code[(address) >> 12] && blocks[(address) >> 12]->block[((address) & 0xFFF) / 4].ops != current_instruction_table.NOTCOMPILED) \
     invalid_code[(address) >> 12] = 1;

static void process_opcode_out(void *strm, const char *fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  char *arg;
  char buff[256];
  
  if(num_decoded==0)
    {
      if(strcmp(fmt,"%s")==0)
    {
      arg = va_arg(ap, char*);
      strcpy(opcode_recompiled[lines_recompiled],arg);
    }
      else
    strcpy(opcode_recompiled[lines_recompiled],"OPCODE-X");
      num_decoded++;
      *(args_recompiled[lines_recompiled])=0;
    }
  else
    {
      vsprintf(buff, fmt, ap);
      sprintf(args_recompiled[lines_recompiled],"%s%s", 
          args_recompiled[lines_recompiled],buff);
    }
  va_end(ap);
}

// Callback function that will be called by libopcodes to read the 
// bytes to disassemble ('read_memory_func' member of 'disassemble_info').
static int read_memory_func(bfd_vma memaddr, bfd_byte *myaddr, 
                            unsigned int length, disassemble_info *info) {
  char* from = (char*)(long)(memaddr);
  char* to =   (char*)myaddr;
  
  while (length-- != 0) {
    *to++ = *from++;
  }
  return (0);
}

void init_host_disassembler(void){


  INIT_DISASSEMBLE_INFO(dis_info, stderr, process_opcode_out);
  dis_info.fprintf_func = (fprintf_ftype) process_opcode_out;
  dis_info.stream = stderr;
  dis_info.bytes_per_line=1;
  dis_info.endian = 1;
  dis_info.mach = bfd_mach_i386_i8086;
  dis_info.disassembler_options = (char*) "i386,suffix";
  dis_info.read_memory_func = read_memory_func;
}

static void decode_recompiled(uint32 addr)
{
    unsigned char *assemb, *end_addr;

    lines_recompiled=0;

    if(blocks[addr>>12] == NULL)
        return;

    if(blocks[addr>>12]->block[(addr&0xFFF)/4].ops == current_instruction_table.NOTCOMPILED)
    //      recompile_block((int *) g_sp_mem, blocks[addr>>12], addr);
      {
    strcpy(opcode_recompiled[0],"INVLD");
    strcpy(args_recompiled[0],"NOTCOMPILED");
    opaddr_recompiled[0] = (void *) 0;
    addr_recompiled=0;
    lines_recompiled++;
    return;
      }

    assemb = (blocks[addr>>12]->code) + 
      (blocks[addr>>12]->block[(addr&0xFFF)/4].local_addr);

    end_addr = blocks[addr>>12]->code;

    if( (addr & 0xFFF) >= 0xFFC)
        end_addr += blocks[addr>>12]->code_length;
    else
        end_addr += blocks[addr>>12]->block[(addr&0xFFF)/4+1].local_addr;

    while(assemb < end_addr)
      {
        opaddr_recompiled[lines_recompiled] = assemb;
        num_decoded=0;

        assemb += print_insn_i386((bfd_vma)(long) assemb, &dis_info);

        lines_recompiled++;
      }

    addr_recompiled = addr;
}

char* get_recompiled_opcode(uint32 addr, int index)
{
    if(addr != addr_recompiled)
        decode_recompiled(addr);

    if(index < lines_recompiled)
        return opcode_recompiled[index];
    else
        return NULL;
}

char* get_recompiled_args(uint32 addr, int index)
{
    if(addr != addr_recompiled)
        decode_recompiled(addr);

    if(index < lines_recompiled)
        return args_recompiled[index];
    else
        return NULL;
}

void * get_recompiled_addr(uint32 addr, int index)
{
    if(addr != addr_recompiled)
        decode_recompiled(addr);

    if(index < lines_recompiled)
        return opaddr_recompiled[index];
    else
        return 0;
}

int get_num_recompiled(uint32 addr)
{
    if(addr != addr_recompiled)
        decode_recompiled(addr);

    return lines_recompiled;
}

int get_has_recompiled(uint32 addr)
{
    unsigned char *assemb, *end_addr;

    if(r4300emu != CORE_DYNAREC || blocks[addr>>12] == NULL)
        return FALSE;

    assemb = (blocks[addr>>12]->code) + 
      (blocks[addr>>12]->block[(addr&0xFFF)/4].local_addr);

    end_addr = blocks[addr>>12]->code;

    if( (addr & 0xFFF) >= 0xFFC)
        end_addr += blocks[addr>>12]->code_length;
    else
        end_addr += blocks[addr>>12]->block[(addr&0xFFF)/4+1].local_addr;
    if(assemb==end_addr)
      return FALSE;

    return TRUE;
}

#else

#define CHECK_MEM(address)

int get_num_recompiled(uint32 addr)
{
    return 0;
}

char* get_recompiled_opcode(uint32 addr, int index)
{
    return NULL;
}

char* get_recompiled_args(uint32 addr, int index)
{
    return NULL;
}

void * get_recompiled_addr(uint32 addr, int index)
{
    return 0;
}

int get_has_recompiled(uint32 addr)
{
    return 0;
}

void init_host_disassembler(void)
{

}

#endif

uint64 read_memory_64(uint32 addr)
{
    return ((uint64)read_memory_32(addr) << 32) | (uint64)read_memory_32(addr + 4);
}

uint64 read_memory_64_unaligned(uint32 addr)
{
    uint64 w[2];
    
    w[0] = read_memory_32_unaligned(addr);
    w[1] = read_memory_32_unaligned(addr + 4);
    return (w[0] << 32) | w[1];
}

void write_memory_64(uint32 addr, uint64 value)
{
    write_memory_32(addr, (uint32) (value >> 32));
    write_memory_32(addr + 4, (uint32) (value & 0xFFFFFFFF));
}

void write_memory_64_unaligned(uint32 addr, uint64 value)
{
    write_memory_32_unaligned(addr, (uint32) (value >> 32));
    write_memory_32_unaligned(addr + 4, (uint32) (value & 0xFFFFFFFF));
}

uint32 read_memory_32(uint32 addr){
   uint32_t offset;

  switch(get_memory_type(addr))
    {
    case M64P_MEM_NOMEM:
      if(tlb_LUT_r[addr>>12])
        return read_memory_32((tlb_LUT_r[addr>>12]&0xFFFFF000)|(addr&0xFFF));
      return M64P_MEM_INVALID;
    case M64P_MEM_RDRAM:
      return g_rdram[(addr & 0xFFFFFF) >> 2];
    case M64P_MEM_RSPMEM:
      return g_sp_mem[(addr & 0x1fff) >> 2];
    case M64P_MEM_ROM:
      return *((uint32 *)(rom + (addr & 0x03FFFFFF)));
    case M64P_MEM_RDRAMREG:
      offset = (addr & 0x3ff) >> 2;
      if (offset < RDRAM_REGS_COUNT)
         return g_rdram_regs[offset];
      break;
    case M64P_MEM_RSPREG:
      offset = (addr & 0xffff) >> 2;
      if (offset < SP_REGS_COUNT)
         return g_sp_regs[offset];
      break;
    case M64P_MEM_RSP:
      offset = (addr & 0xffff) >> 2;
      if (offset < SP_REGS2_COUNT)
         return g_sp_regs2[offset];
      break;
    case M64P_MEM_DP:
      offset = (addr & 0xffff) >> 2;
      if (offset < DPC_REGS_COUNT)
         return g_dpc_regs[offset];
      break;
    case M64P_MEM_DPS:
      if (addrlow < 0x10)
        return *(readdps[addrlow&0xfffc]);
      break;
    case M64P_MEM_VI:
      offset = (addr & 0xffff) >> 2;
      if (offset < VI_REGS_COUNT)
         return g_vi_regs[offset];
      break;
    case M64P_MEM_AI:
      offset = (addr & 0xffff) >> 2;
      if (offset < AI_REGS_COUNT)
         return g_ai_regs[offset];
      break;
    case M64P_MEM_PI:
      offset = (addr & 0xffff) >> 2;
      if (offset < PI_REGS_COUNT)
         return g_pi_regs[offset];
      break;
    case M64P_MEM_RI:
      offset = ri_reg(addr);
      if (offset < RI_REGS_COUNT)
         return g_ri.regs[offset];
      break;
    case M64P_MEM_SI:
      offset = (addr & 0xffff) >> 2;
      if (offset < SI_REGS_COUNT)
         return g_si_regs[offset];
      break;
    case M64P_MEM_PIF:
      offset = ((addr & 0xffff) - 0x7c0);
      if (offset < PIF_RAM_SIZE)
         return sl((*((uint32_t*)&g_pif_ram[offset])));
      break;
    case M64P_MEM_MI:
      offset = (addr & 0xffff) >> 2;
      if (offset < MI_REGS_COUNT)
         return g_mi_regs[offset];
      break;
    default:
      break;
    }
    return M64P_MEM_INVALID;
}

uint32 read_memory_32_unaligned(uint32 addr)
{
    uint8 i, b[4];
    
    for(i=0; i<4; i++) b[i] = read_memory_8(addr + i);
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

void write_memory_32(uint32 addr, uint32 value){
  switch(get_memory_type(addr))
    {
    case M64P_MEM_RDRAM:
       g_rdram[(addr & 0xFFFFFF) >> 2] = value;
      CHECK_MEM(addr)
      break;
    }
}

void write_memory_32_unaligned(uint32 addr, uint32 value)
{
    write_memory_8(addr + 0, value >> 24);
    write_memory_8(addr + 1, (value >> 16) & 0xFF);
    write_memory_8(addr + 2, (value >> 8) & 0xFF);
    write_memory_8(addr + 3, value & 0xFF);
}

//read_memory_16_unaligned and write_memory_16_unaligned don't exist because
//read_memory_16 and write_memory_16 work unaligned already.
uint16 read_memory_16(uint32 addr)
{
    return ((uint16)read_memory_8(addr) << 8) | (uint16)read_memory_8(addr+1); //cough cough hack hack
}

void write_memory_16(uint32 addr, uint16 value)
{
    write_memory_8(addr, value >> 8); //this isn't much better
    write_memory_8(addr + 1, value & 0xFF); //then again, it works unaligned
}

uint8 read_memory_8(uint32 addr)
{
    uint32 word;
    
    word = read_memory_32(addr & ~3);
    return (word >> ((3 - (addr & 3)) * 8)) & 0xFF;
}

void write_memory_8(uint32 addr, uint8 value)
{
    uint32 word, mask;
    
    word = read_memory_32(addr & ~3);
    mask = 0xFF << ((3 - (addr & 3)) * 8);
    word = (word & ~mask) | (value << ((3 - (addr & 3)) * 8));
    write_memory_32(addr & ~3, word);
}

uint32 get_memory_flags(uint32 addr)
{
  int type=get_memory_type(addr);
  const uint32 addrlow = (addr & 0xFFFF);
  uint32 flags = 0;

  switch(type)
  {
    case M64P_MEM_NOMEM:
      if(tlb_LUT_r[addr>>12])
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_NOTHING:
      if (((addr >> 16) == 0x8801 || (addr >> 16 == 0xA801)) && addrlow == 0)
        flags = M64P_MEM_FLAG_WRITABLE_EMUONLY; // for flashram command
      break;
    case M64P_MEM_RDRAM:
      flags = M64P_MEM_FLAG_WRITABLE;
    case M64P_MEM_ROM:
      flags |= M64P_MEM_FLAG_READABLE;
      break;
    case M64P_MEM_RDRAMREG:
      if (addrlow < 0x28)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_RSPMEM:
      if (addrlow < 0x2000)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_RSPREG:
      if (addrlow < 0x20)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_RSP:
      if (addrlow < 0x8)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_DP:
      if (addrlow < 0x20)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_DPS:
      if (addrlow < 0x10)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_VI:
      if (addrlow < 0x38)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_AI:
      if (addrlow < 0x18)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_PI:
      if (addrlow < 0x34)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_RI:
      if (addrlow < 0x20)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_SI:
      if (addrlow < 0x1c)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_FLASHRAMSTAT:
      if (addrlow == 0)
        flags = M64P_MEM_FLAG_READABLE_EMUONLY;
      break;
    case M64P_MEM_PIF:
      if (addrlow >= 0x7C0 && addrlow <= 0x7FF)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    case M64P_MEM_MI:
      if (addrlow < 0x10)
        flags = M64P_MEM_FLAG_READABLE | M64P_MEM_FLAG_WRITABLE_EMUONLY;
      break;
    default:
      break;
  }

  return flags;
}
