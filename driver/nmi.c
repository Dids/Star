#include "main.h"
#include "io.h"

void NMI_enable() {
	outb(0x70, inb(0x70)&0x7F);
}

void NMI_disable() {
	outb(0x70, inb(0x70)|0x80);
}