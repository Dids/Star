/*
 * File: arp.c
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

#include <main.h>
#include <string.h>

#include <kernel/memory/kheap.h>
#include <kernel/networking/protocols/arp.h>
#include <kernel/networking/networking.h>

uint16_t swap_uint16( uint16_t val ) 
{
    return (val << 8) | (val >> 8 );
}

arp_frame_t* arp_request(uint8_t* SenderMAC, uint8_t* TargetIP) {
	// Allocate memory for frame
	arp_frame_t *frame = (arp_frame_t*)kheap_alloc(sizeof(arp_frame_t));

	// Clear frame with 0s
	memset(frame, 0, sizeof(arp_frame_t));

	// Fill out ARP frame details
	frame->HardwareType = swap_uint16(1);
	frame->ProtocolType = swap_uint16(0x0800);
	frame->HardwareSize = 6;
	frame->ProtocolSize = 4;
	frame->Opcode = swap_uint16(1);
	memcpy((frame->SenderMAC), SenderMAC, NET_MAC_LENGTH);
	for (int x = 0; x < NET_IPV4_LENGTH; x++)
        frame->SenderIP[x] = 0x00;
    for (int x = 0; x < NET_MAC_LENGTH; x++)
        frame->TargetMAC[x] = 0x00;
    memcpy((frame->TargetIP), TargetIP, 4);

    return frame;
}