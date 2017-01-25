/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2013 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows 
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <string.h> // for memcpy
#include <arm_math.h>
#include "AudioStream.h"


audio_block_t * AudioStream::memory_pool;
uint32_t AudioStream::memory_pool_available_mask[6];

uint16_t AudioStream::cpu_cycles_total = 0;
uint16_t AudioStream::cpu_cycles_total_max = 0;
uint8_t AudioStream::memory_used = 0;
uint8_t AudioStream::memory_used_max = 0;

//Uncomment to disable debug printing
//#define AUDIO_DBG

#ifdef AUDIO_DBG
#include <Arduino.h>
#define AUDIO_DBG_PRINT(x) if (Serial) { Serial.print(x); Serial.flush(); }
#define AUDIO_DBG_PRINTF(x,y) if (Serial) { Serial.printf(x,y); Serial.flush(); }
#else
#define AUDIO_DBG_PRINT(x)
#define AUDIO_DBG_PRINTF(x,y)
#endif



// Set up the pool of audio data blocks
// placing them all onto the free list
void AudioStream::initialize_memory(audio_block_t *data, unsigned int num)
{
	unsigned int i;

	//Serial.println("AudioStream initialize_memory");
	//delay(10);
	if (num > 192) num = 192;
	__disable_irq();
	memory_pool = data;
	for (i=0; i < 6; i++) {
		memory_pool_available_mask[i] = 0;
	}
	for (i=0; i < num; i++) {
		memory_pool_available_mask[i >> 5] |= (1 << (i & 0x1F));
	}
	for (i=0; i < num; i++) {
		data[i].memory_pool_index = i;
	}
	__enable_irq();

}

// Allocate 1 audio data block.  If successful
// the caller is the only owner of this new block
audio_block_t * AudioStream::allocate(void)
{
	uint32_t n, index, avail;
	uint32_t *p;
	audio_block_t *block;
	uint8_t used;

	p = memory_pool_available_mask;
	__disable_irq();
	do {
		avail = *p; if (avail) break;
		p++; avail = *p; if (avail) break;
		p++; avail = *p; if (avail) break;
		p++; avail = *p; if (avail) break;
		p++; avail = *p; if (avail) break;
		p++; avail = *p; if (avail) break;
		__enable_irq();
		//Serial.println("alloc:null");
		return NULL;
	} while (0);
	n = __builtin_clz(avail);
	*p = avail & ~(0x80000000 >> n);
	used = memory_used + 1;
	memory_used = used;
	__enable_irq();
	index = p - memory_pool_available_mask;
	block = memory_pool + ((index << 5) + (31 - n));
	block->ref_count = 1;
#ifdef AUDIO_FLOAT
	block->nextBlock = NULL;
	block->type = AUDIO_BLOCK_INT16;
#endif
	if (used > memory_used_max) memory_used_max = used;
//	Serial.print("alloc:"); Serial.printf(" type: %d   ", block->type);
//	Serial.println((uint32_t)block, HEX);
	return block;
}

#ifdef AUDIO_FLOAT

// Allocate 2 chained audio blocks,  If successful
// the caller is the only owner of these new blocks
audio_block_t * AudioStream::allocateFloat(void) {
//	Serial.print("float:");
	audio_block_t * block1 = allocate();
	if (block1 == NULL) return NULL;

	//AUDIO_DBG_PRINT("Allocatefloat: block1-ok \n");
//	Serial.print("float:");
	audio_block_t * block2 = allocate();
	if (block2 == NULL) {
		release(block1);
		return NULL;
	}
	//AUDIO_DBG_PRINT("Allocatefloat: block2-ok \n");
	block1->type = AUDIO_BLOCK_FLOAT;
	block1->nextBlock = block2;
	block2->type = AUDIO_BLOCK_FLOAT;
	block2->nextBlock = NULL;

	return block1;
}
#endif


// Release ownership of a data block.  If no
// other streams have ownership, the block is
// returned to the free pool
void AudioStream::release(audio_block_t *block)
{
	uint32_t mask = (0x80000000 >> (31 - (block->memory_pool_index & 0x1F)));
	uint32_t index = block->memory_pool_index >> 5;

	__disable_irq();

#ifdef AUDIO_FLOAT
	if (block->type == AUDIO_BLOCK_FLOAT && block->nextBlock != NULL) {
	//	AUDIO_DBG_PRINT("release float\n");
		audio_block_t * block2 = block->nextBlock;

//		Serial.print("release:"); Serial.printf(" type: %d   ", block2->type);
//		Serial.println((uint32_t)block2, HEX);


		uint32_t mask2 = (0x80000000 >> (31 - (block2->memory_pool_index & 0x1F)));
		uint32_t index2 = block2->memory_pool_index >> 5;

		if (block2->ref_count > 1) {
			block2->ref_count--;
		} else {
			//Serial.print("reles:");
			//Serial.println((uint32_t)block, HEX);
			memory_pool_available_mask[index2] |= mask2;
			memory_used--;
		}
	}
#endif
//	AUDIO_DBG_PRINT("release block\n");
//	Serial.print("release:"); Serial.printf(" type: %d   ", block->type);
//	Serial.println((uint32_t)block, HEX);

	if (block->ref_count > 1) {
		block->ref_count--;
	} else {
		//Serial.print("reles:");
		//Serial.println((uint32_t)block, HEX);
		memory_pool_available_mask[index] |= mask;
		memory_used--;
	}


	__enable_irq();
}



// Transmit an audio data block
// to all streams that connect to an output.  The block
// becomes owned by all the recepients, but also is still
// owned by this object.  Normally, a block must be released
// by the caller after it's transmitted.  This allows the
// caller to transmit to same block to more than 1 output,
// and then release it once after all transmit calls.
void AudioStream::transmit(audio_block_t *block, unsigned char index)
{
#ifdef AUDIO_FLOAT
	audio_block_t * cached_float = NULL;
	audio_block_t * cached_int = NULL;
#endif

	for (AudioConnection *c = destination_list; c != NULL; c = c->next_dest) {
		if (c->src_index == index) {
			if (c->dst.inputQueue[c->dest_index] == NULL) {
#ifndef AUDIO_FLOAT
				c->dst.inputQueue[c->dest_index] = block;
				block->ref_count++;
#else
				if (this->type == c->dst.type) { //No conversion
					c->dst.inputQueue[c->dest_index] = block;
					block->ref_count++;
					if (block->nextBlock) block->nextBlock->ref_count++;
				} else if (this->type == AUDIO_STREAM_INT16) { //Convert int to float
					AUDIO_DBG_PRINT("int to float: \n");

					if (cached_float == NULL) { // First time we need to do an conversion
						cached_float = allocateFloat();
						if (cached_float != NULL) {
							arm_q15_to_float( (q15_t *) &block->data[0], (float *) &cached_float->data[0], AUDIO_BLOCK_SAMPLES/2);
							arm_q15_to_float( (q15_t *) &block->data[AUDIO_BLOCK_SAMPLES/2], (float *) &cached_float->nextBlock->data[0], AUDIO_BLOCK_SAMPLES/2);

							//We must start at 0 with these blocks as the transmitter will not call release on them
							cached_float->ref_count--;
							cached_float->nextBlock->ref_count--;
						}
					}
					c->dst.inputQueue[c->dest_index] = cached_float;
					if (cached_float != NULL) { //Alloc might have failed
						cached_float->ref_count++;
						if (cached_float->nextBlock) cached_float->nextBlock->ref_count++;

					}
					AUDIO_DBG_PRINT("end int to float \n ");


				} else { //Convert float to int

					AUDIO_DBG_PRINT("float to int\n");

					if (cached_int == NULL) {
						cached_int = allocate();
						if (cached_int != NULL) {
							arm_float_to_q15((float *) &block->data[0], (q15_t *) &cached_int->data[0], AUDIO_BLOCK_SAMPLES/2);
							arm_float_to_q15((float *) &block->nextBlock->data[0], (q15_t *) &cached_int->data[AUDIO_BLOCK_SAMPLES/2], AUDIO_BLOCK_SAMPLES/2);
							//We must start at 0 with these blocks as the transmitter will not call release on them
							cached_int->ref_count--;
						}
					}
					c->dst.inputQueue[c->dest_index] = cached_int;
					if (cached_int != NULL) {
						cached_int->ref_count++;

					}
					AUDIO_DBG_PRINT("end float to int\n");

				}
#endif
			}
		}
	}
}


// Receive block from an input.  The block's data
// may be shared with other streams, so it must not be written
audio_block_t * AudioStream::receiveReadOnly(unsigned int index)
{
	audio_block_t *in;

	if (index >= num_inputs) return NULL;
	in = inputQueue[index];
	inputQueue[index] = NULL;
	return in;
}

// Receive block from an input.  The block will not
// be shared, so its contents may be changed.
audio_block_t * AudioStream::receiveWritable(unsigned int index)
{
	audio_block_t *in, *p;

	if (index >= num_inputs) return NULL;
	in = inputQueue[index];
	inputQueue[index] = NULL;
	if (in && in->ref_count > 1) {
		p = allocate();
		if (p) memcpy(p->data, in->data, sizeof(p->data));
		in->ref_count--;
		in = p;
	}
	return in;
}


#ifdef AUDIO_FLOAT

audio_block_t * AudioStream::receiveReadOnlyFloat(unsigned int index) {
	audio_block_t * block = this->receiveReadOnly(index);
	if (block != NULL && block->type == AUDIO_BLOCK_FLOAT)
	{
		return block;
	}
	return NULL;
}

audio_block_t * AudioStream::receiveWritableFloat(unsigned int index) {
	audio_block_t *in,*out=NULL;

	if (index >= num_inputs) return NULL;
	in = inputQueue[index];
	inputQueue[index] = NULL;

	AUDIO_DBG_PRINT("recv writable\n");

	if (in && in->nextBlock && in->ref_count > 1 && in->nextBlock->ref_count > 1) {
		AUDIO_DBG_PRINT("writable copy needed\n");

		out = allocateFloat();
		if (out) {
			memcpy(out->data, in->data, sizeof(out->data));
			memcpy(out->nextBlock->data, in->nextBlock->data, sizeof(out->nextBlock->data));
			AUDIO_DBG_PRINT("writable copy executed\n");

		} else {
			AUDIO_DBG_PRINT("writable copy failed no mem\n");

		}

		in->ref_count--;
		in->nextBlock->ref_count--;

		in = out;
	}

	if (in) {
		AUDIO_DBG_PRINT("return not null");
	}


	return in;
}
#endif

void AudioConnection::connect(void)
{
	AudioConnection *p;

	if (isConnected) return;
	if (dest_index > dst.num_inputs) return;
	__disable_irq();
	p = src.destination_list;
	if (p == NULL) {
		src.destination_list = this;
	} else {
		while (p->next_dest) {
			if (&p->src == &this->src && &p->dst == &this->dst
				&& p->src_index == this->src_index && p->dest_index == this->dest_index) {
				//Source and destination already connected through another connection, abort
				__enable_irq();
				return;
			}
			p = p->next_dest;
		}
		p->next_dest = this;
	}
	this->next_dest = NULL;
	src.numConnections++;
	src.active = true;

	dst.numConnections++;
	dst.active = true;

	isConnected = true;

	__enable_irq();
}

void AudioConnection::disconnect(void)
{
	AudioConnection *p;

	if (!isConnected) return;
	if (dest_index > dst.num_inputs) return;
	__disable_irq();
	// Remove destination from source list
	p = src.destination_list;
	if (p == NULL) {
		return;
	} else if (p == this) {
		if (p->next_dest) {
			src.destination_list = next_dest;
		} else {
			src.destination_list = NULL;
		}
	} else {
		while (p) {
			if (p == this) {
				if (p->next_dest) {
					p = next_dest;
					break;
				} else {
					p = NULL;
					break;
				}
			}
			p = p->next_dest;
		}
	}
	//Remove possible pending src block from destination
	dst.inputQueue[dest_index] = NULL;

	//Check if the disconnected AudioStream objects should still be active
	src.numConnections--;
	if (src.numConnections == 0) {
		src.active = false;
	}

	dst.numConnections--;
	if (dst.numConnections == 0) {
		dst.active = false;
	}

	isConnected = false;

	__enable_irq();
}


// When an object has taken responsibility for calling update_all()
// at each block interval (approx 2.9ms), this variable is set to
// true.  Objects that are capable of calling update_all(), typically
// input and output based on interrupts, must check this variable in
// their constructors.
bool AudioStream::update_scheduled = false;

bool AudioStream::update_setup(void)
{
	if (update_scheduled) return false;
	NVIC_SET_PRIORITY(IRQ_SOFTWARE, 208); // 255 = lowest priority
	NVIC_ENABLE_IRQ(IRQ_SOFTWARE);
	update_scheduled = true;
	return true;
}

void AudioStream::update_stop(void)
{
	NVIC_DISABLE_IRQ(IRQ_SOFTWARE);
	update_scheduled = false;
}

AudioStream * AudioStream::first_update = NULL;

void software_isr(void) // AudioStream::update_all()
{
	AudioStream *p;

	ARM_DEMCR |= ARM_DEMCR_TRCENA;
	ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
	uint32_t totalcycles = ARM_DWT_CYCCNT;
	//digitalWriteFast(2, HIGH);
	for (p = AudioStream::first_update; p; p = p->next_update) {
		if (p->active) {
			uint32_t cycles = ARM_DWT_CYCCNT;
			p->update();
			// TODO: traverse inputQueueArray and release
			// any input blocks that weren't consumed?
			cycles = (ARM_DWT_CYCCNT - cycles) >> 4;
			p->cpu_cycles = cycles;
			if (cycles > p->cpu_cycles_max) p->cpu_cycles_max = cycles;
		}
	}
	//digitalWriteFast(2, LOW);
	totalcycles = (ARM_DWT_CYCCNT - totalcycles) >> 4;;
	AudioStream::cpu_cycles_total = totalcycles;
	if (totalcycles > AudioStream::cpu_cycles_total_max)
		AudioStream::cpu_cycles_total_max = totalcycles;
}

