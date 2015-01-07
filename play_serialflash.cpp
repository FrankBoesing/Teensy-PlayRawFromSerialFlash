/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Modified to play from Serial Flash (c) Frank Bösing, 2014/12
 
#include "play_serialflash.h"
#include "utility/dspinst.h"
#include "spi_interrupt.h"

#define SPICLOCK 30000000
#define SERFLASH_CS 				6	//Chip Select W25Q128FV SPI Flash

void AudioPlaySerialFlash::flashinit(void)
{   
	pinMode(10,OUTPUT);
	digitalWrite(10, HIGH);
	pinMode(SERFLASH_CS,OUTPUT);
	digitalWrite(SERFLASH_CS, HIGH);
	SPI.setMOSI(7);
	SPI.setSCK(14);
	SPI.begin();
	spisettings = SPISettings(SPICLOCK , MSBFIRST, SPI_MODE0);
}

inline void AudioPlaySerialFlash::readSerStart(const size_t position) 
{
	SPI.beginTransaction(spisettings);
	digitalWriteFast(SERFLASH_CS, LOW);
	SPI.transfer(0x0b);//CMD_READ_HIGH_SPEED
	SPI.transfer((position >> 16) & 0xff);
	SPI.transfer((position >> 8) & 0xff); 
	SPI.transfer(position & 0xff);
	SPI.transfer(0);
}

inline void AudioPlaySerialFlash::readSerDone(void) 
{
	digitalWriteFast(SERFLASH_CS, HIGH);
	SPI.endTransaction();
}

void AudioPlaySerialFlash::play(const unsigned int data)
{
	int temp;
	AudioStartUsingSPI();
	readSerStart(data);
	length = SPI.transfer(0);
	length |= (uint16_t) SPI.transfer(0) <<8;
	length |= (uint32_t) SPI.transfer(0) <<16;
	temp = SPI.transfer(0);
	readSerDone();
	prior = 0;
	next = 0;
	beginning = data + 4;
	__disable_irq();
	playing = temp;
	__enable_irq();
}

void AudioPlaySerialFlash::stop(void)
{	__disable_irq();
	playing = 0;
	__enable_irq();
	AudioStopUsingSPI();
}

extern "C" {
extern const int16_t ulaw_decode_table[256];
};


void AudioPlaySerialFlash::update(void)
{
	audio_block_t *block;
	int16_t *out;
	uint32_t consumed;
	int16_t s0, s1, s2, s3, s4;
	uint16_t a,b;
	int i;

	if (!playing) return;
	block = allocate();
	if (block == NULL) return;

	out = block->data;
	s0 = prior;
	
	readSerStart(beginning + next);		
	
	switch (playing) {

	  case 0x01: // u-law encoded, 44100 Hz		
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {	
			*out++ = ulaw_decode_table[SPI.transfer(0)];
		}
		consumed = 128;
		break;

	  case 0x02: // u-law encoded, 22050 Hz 
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 8) {			
			s1 = ulaw_decode_table[SPI.transfer(0)];
			s2 = ulaw_decode_table[SPI.transfer(0)];
			s3 = ulaw_decode_table[SPI.transfer(0)];
			s4 = ulaw_decode_table[SPI.transfer(0)];
			*out++ = (s0 + s1) >> 1;
			*out++ = s1;
			*out++ = (s1 + s2) >> 1;
			*out++ = s2;
			*out++ = (s2 + s3) >> 1;
			*out++ = s3;
			*out++ = (s3 + s4) >> 1;
			*out++ = s4;
			s0 = s4;
		}
		consumed = 64;
		break;

		case 0x03: // u-law encoded, 11025 Hz
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 16) {
			s1 = ulaw_decode_table[SPI.transfer(0)];
			s2 = ulaw_decode_table[SPI.transfer(0)];
			s3 = ulaw_decode_table[SPI.transfer(0)];
			s4 = ulaw_decode_table[SPI.transfer(0)];
			*out++ = (s0 * 3 + s1) >> 2;
			*out++ = (s0 + s1)     >> 1;
			*out++ = (s0 + s1 * 3) >> 2;
			*out++ = s1;
			*out++ = (s1 * 3 + s2) >> 2;
			*out++ = (s1 + s2)     >> 1;
			*out++ = (s1 + s2 * 3) >> 2;
			*out++ = s2;
			*out++ = (s2 * 3 + s3) >> 2;
			*out++ = (s2 + s3)     >> 1;
			*out++ = (s2 + s3 * 3) >> 2;
			*out++ = s3;
			*out++ = (s3 * 3 + s4) >> 2;
			*out++ = (s3 + s4)     >> 1;
			*out++ = (s3 + s4 * 3) >> 2;
			*out++ = s4;
			s0 = s4;
		}
		consumed = 32;
		break;

		case 0x81: // 16 bit PCM, 44100 Hz	
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {			
			a = SPI.transfer(0);
			b = SPI.transfer(0);		
			*out++ = a | b<<8;
		}
		consumed = 256;
		break;
		
		case 0x82: // 16 bits PCM, 22050 Hz		
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 4) {
			a = SPI.transfer(0);
			b = SPI.transfer(0);	
			s1 = a | b<<8;
			a = SPI.transfer(0);
			b = SPI.transfer(0);	
			s2 = a | b<<8;			
			*out++ = (s0 + s1) >> 1;
			*out++ = s1;
			*out++ = (s1 + s2) >> 1;
			*out++ = s2;
			s0 = s2;
		}
		consumed = 128;
		break;

		case 0x83: // 16 bit PCM, 11025 Hz		
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i += 8) {
			a = SPI.transfer(0);
			b = SPI.transfer(0);	
			s1 = a | b<<8;
			a = SPI.transfer(0);
			b = SPI.transfer(0);	
			s2 = a | b<<8;					
			*out++ = (s0 * 3 + s1) >> 2;
			*out++ = (s0 + s1)     >> 1;
			*out++ = (s0 + s1 * 3) >> 2;
			*out++ = s1;
			*out++ = (s1 * 3 + s2) >> 2;
			*out++ = (s1 + s2)     >> 1;
			*out++ = (s1 + s2 * 3) >> 2;
			*out++ = s2;
			s0 = s2;
		}
		consumed = 64;
		break;

	  default:
		release(block);
		playing = 0;
		return;
	}
	
	readSerDone();
	
	prior = s0;
	next += consumed;
	if (length > consumed) {
		length -= consumed;
	} else {
		stop();
	}

	transmit(block);
	release(block);
}


#define B2M_88200 (uint32_t)((double)4294967296000.0 / AUDIO_SAMPLE_RATE_EXACT / 2.0)
#define B2M_44100 (uint32_t)((double)4294967296000.0 / AUDIO_SAMPLE_RATE_EXACT) // 97352592
#define B2M_22050 (uint32_t)((double)4294967296000.0 / AUDIO_SAMPLE_RATE_EXACT * 2.0)
#define B2M_11025 (uint32_t)((double)4294967296000.0 / AUDIO_SAMPLE_RATE_EXACT * 4.0)


uint32_t AudioPlaySerialFlash::positionMillis(void)
{
	uint8_t p;
	uint32_t n,b;
	uint32_t b2m;

	__disable_irq();
	p = playing;
	n = next;
	b = beginning;
	__enable_irq();
	switch (p) {
	  case 0x81: // 16 bit PCM, 44100 Hz
		b2m = B2M_88200;  break;
	  case 0x01: // u-law encoded, 44100 Hz
	  case 0x82: // 16 bits PCM, 22050 Hz
		b2m = B2M_44100;  break;
	  case 0x02: // u-law encoded, 22050 Hz
	  case 0x83: // 16 bit PCM, 11025 Hz
		b2m = B2M_22050;  break;
	  case 0x03: // u-law encoded, 11025 Hz
		b2m = B2M_11025;  break;
	  default:
		return 0;
	}
	if (p == 0) return 0;
	return ((uint64_t)(n - b) * b2m) >> 32;
}

uint32_t AudioPlaySerialFlash::lengthMillis(void)
{
	uint8_t p;
	uint32_t b;
	uint32_t b2m;

	__disable_irq();
	p = playing;
	b = beginning;
	__enable_irq();
	switch (p) {
	  case 0x81: // 16 bit PCM, 44100 Hz
	  case 0x01: // u-law encoded, 44100 Hz
		b2m = B2M_44100;  break;
	  case 0x82: // 16 bits PCM, 22050 Hz
	  case 0x02: // u-law encoded, 22050 Hz
		b2m = B2M_22050;  break;
	  case 0x83: // 16 bit PCM, 11025 Hz
	  case 0x03: // u-law encoded, 11025 Hz
		b2m = B2M_11025;  break;
	  default:
		return 0;
	}
	return ((uint64_t)((b - 1) & 0xFFFFFF) * b2m) >> 32;
}


