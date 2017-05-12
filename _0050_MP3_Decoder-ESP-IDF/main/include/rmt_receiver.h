/*
 * rmt_receiver.h
 *
 *  Created on: 07.04.2017
 *      Author: NooM
 */

#ifndef _RMT_RECEIVER_H_
#define _RMT_RECEIVER_H_

typedef struct {
		uint16_t	address;      // Used by PANASONIC & Sharp [16-bits]
		uint32_t	value;        // Decoded value [max 32-bits]
		uint8_t		bits;         // Number of bits in decoded value
} decode_results_t;

#define PANASONIC_power				0x0100BCBD
#define PANASONIC_ch_up				0x01002C2D
#define PANASONIC_ch_down			0x0100ACAD
#define PANASONIC_vol_up			0x01000405
#define PANASONIC_vol_down			0x01008485
#define PANASONIC_ok				0x01009293
#define PANASONIC_tv_av				0x0100A0A1
#define PANASONIC_menu				0x01004A4B
#define PANASONIC_return			0x0100ECED
#define PANASONIC_n					0x01003031
#define PANASONIC_1					0x01000809
#define PANASONIC_2					0x01008889
#define PANASONIC_3					0x01004849
#define PANASONIC_4					0x0100c8c9
#define PANASONIC_5					0x01002829
#define PANASONIC_6					0x0100a8a9
#define PANASONIC_7					0x01006869
#define PANASONIC_8					0x0100e8e9
#define PANASONIC_9					0x01001819
#define PANASONIC_charp				0x01009c9d
#define PANASONIC_0					0x01009899
#define PANASONIC_star				0x0100dcdd

void start_task_rmt_receiver();


#endif
