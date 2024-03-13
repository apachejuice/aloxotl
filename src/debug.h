// apachejuice, 25.02.2024
// See LICENSE for details.
#ifndef __ALOXOTL_DEBUG__
#define __ALOXOTL_DEBUG__
#include "chunk.h"

void disassemble_chunk (chunk *chunk, const char *name);
int  disassemble_instruction (chunk *chunk, size offset);

#endif
