/*
 * ******************************************************************
 * ZYNTHIAN PROJECT: Zynpot, wrapper library for rotaries
 *
 * Library for interfacing rotaries of several types
 *
 * Copyright (C) 2015-2021 Fernando Moyano <jofemodo@zynthian.org>
 *
 * ******************************************************************
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the LICENSE.txt file.
 *
 * ******************************************************************
 */

#include <lo/lo.h>

//-----------------------------------------------------------------------------
// Zynpot data
//-----------------------------------------------------------------------------

#define ZYNPOT_NONE 0
#define ZYNPOT_ZYNCODER 1
#define ZYNPOT_RV112 2

#define MAX_NUM_ZYNPOTS 4

typedef struct zynpot_data_st {
	uint8_t enabled;            // 1 to enable encoder
	int32_t min_value;          // Upper range value
	int32_t max_value;          // Lower range value
	int32_t step;               // Size of change in value for each detent of encoder
	uint8_t inv;                // 1 to invert range
	int32_t value;              // Current encdoder value [min_value..max_value]
	uint8_t value_flag;         // 1 if value changed since last read
	int8_t zpot_i;              // Zynpot index assigned to this encoder
} zynpot_data_t;


typedef struct zynpot_st {
	uint8_t type;               // Type of encoder [#define ZYNPOT_NONE | ZYNPOT_ZYNCODER | define ZYNPOT_RV112]
	zynpot_data_t *data;        // Pointer to instance of zynpot_data_t structure

	uint8_t midi_chan;          // MIDI channel to send CC messages
	uint8_t midi_cc;            // MIDI CC number

	uint16_t osc_port;          // OSC UDP port
	lo_address osc_lo_addr;     // OSC desintation address (only one?)
	char osc_path[512];         // OSC path

	// Function pointers
	int (*setup_rangescale)(uint8_t, int32_t, int32_t, int32_t, int32_t);
	int32_t (*get_value)(uint8_t);
	uint8_t (*get_value_flag)(uint8_t);
	int (*set_value)(uint8_t, int32_t);
} zynpot_t;
zynpot_t zynpots[MAX_NUM_ZYNPOTS];


#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------
// Zynpot common API
//-----------------------------------------------------------------------------

/** @brief  Reset all zynpots to default configuratoin
*/
void reset_zynpots();

/** @brief  Get quantity of enabled zynpots
*   @retval int Quantity of enabled zynpots [0..MAX_NUM_ZYNCODERS]
*/
int get_num_zynpots();

/** @brief  Configure zynpot
*   @param  zynpot Index of zynpot [0..MAX_NUM_ZYNCODERS]
*   @param  type Zynpot type [ZYNPOT_ZYNCODER | ZYNPOT_RV112]
*   @retval int 1 on success, 0 on failure
*/
int setup_zynpot(uint8_t zynpot, uint8_t type);

/** @brief  Configure zynpot
*   @param  zynpot Index of zynpot [0..MAX_NUM_ZYNCODERS]
*   @param  min_value Lower bound of value range
*   @param  max_value Upper bound of value range
*   @param  value Current value to assign to encoder
*   @param  step Size of change in value for each detent of encoder
*   @retval int 1 on success, 0 on failure
*/
int setup_rangescale_zynpot(uint8_t zynpot, int32_t min_value, int32_t max_value, int32_t value, int32_t step);

/** @brief  Get current value
*   @param  zynpot Index of zynpot [0..MAX_NUM_ZYNCODERS]
*   @retval int32_t Current value
*/
int32_t get_value_zynpot(uint8_t zynpot);

/** @brief  Check if value has changed since last read
*   @param  zynpot Index of zynpot [0..MAX_NUM_ZYNCODERS]
*   @retval uint8_t 1 if value changed since last read.
*/
uint8_t get_value_flag_zynpot(uint8_t zynpot);

/** @brief  Set value
*   @param  zynpot Index of zynpot
*   @param  value New value
*   @param  send 1 to send new value to configured MIDI CC / OSC
*   @retval int 1 on success, 0 on failure
*/
int set_value_zynpot(uint8_t zynpot, int32_t value, int send);

//-----------------------------------------------------------------------------
// Zynpot MIDI & OSC API
//-----------------------------------------------------------------------------

/** @brief  Assign MIDI CC to zynpot
*   @param  zynpot Index of zynpot
*   @param  midi_chan MIDI channel 90..15)
*   @param  midi_cc MIDI CC number
*   @retval int 1 on success, 0 on failure
*/
int setup_midi_zynpot(uint8_t zynpot, uint8_t midi_chan, uint8_t midi_cc);

/** @brief  Assign OSC path to zynpot
*   @param  zynpot Index of zynpot
*   @param  osc_path OSC path
*   @retval int 1 on success, 0 on failure
*/
int setup_osc_zynpot(uint8_t zynpot, char *osc_path);

/** @brief  Send zynpot value to MIDI and OSC (if configured)
*   @param  zynpot Index of zynpot
*   @retval int 1 on success, 0 on failure
*/
int send_zynpot(uint8_t zynpot);

/** @brief  Handle MIDI CC event, updates zynpot value
*   @param  midi_chan MIDI channel [0..15]
*   @param  midi_cc MIDI CC number [0..127]
*   @param  val MIDI CC value [0..127]
*   @retval int 1 on success, 0 on failure
*/
int midi_event_zynpot(uint8_t midi_chan, uint8_t midi_cc, uint8_t value);

//-----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

