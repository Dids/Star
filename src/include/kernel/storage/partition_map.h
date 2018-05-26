/*
 * File: partition_map.h
 * 
 * Copyright (c) 2017-2018 Sydney Erickson, John Davis
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PARTITION_MAP_H
#define PARTITION_MAP_H

#include <main.h>

#define PARTITION_MAP_TYPE_MBR  1
#define PARTITION_MAP_TYPE_GPT  2

#define FILESYSTEM_TYPE_UNKNOWN 0
#define FILESYSTEM_TYPE_FAT     1
#define FILESYSTEM_TYPE_FAT32   2

#define PARTITION_NONE      0xFFFF

typedef struct {
    uint16_t FsType;
    uint64_t LbaStart;
    uint64_t LbaEnd;
} partition_t;

typedef struct {
    uint8_t Type;
    uint16_t NumPartitions;
    partition_t **Partitions;
} partition_map_t;

extern void part_print_map(partition_map_t *partMap);

#endif
