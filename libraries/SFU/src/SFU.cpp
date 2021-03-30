#include "SFU.h"

const unsigned char SFU[0x20000] __attribute__ ((section(".second_stage_ota"), used)) = {
	#include "rp2040.h"
};
