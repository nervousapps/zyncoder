/*
 * ******************************************************************
 * ZYNTHIAN PROJECT: Zyncoder Library
 *
 * Library for interfacing Rotary Encoders & Switches connected
 * to RBPi native GPIOs or expanded with MCP23008/MCP23017.
 * Includes an emulator mode for developing on desktop computers.
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
#include <wiringPi.h>

#include "zynmidirouter.h"

//-----------------------------------------------------------------------------
// MCP23017 stuff
//-----------------------------------------------------------------------------
#ifndef MCP23008_ENCODERS

struct wiringPiNodeStruct * init_mcp23017(int base_pin, uint8_t i2c_address, uint8_t inta_pin, uint8_t intb_pin, void (*isrs[2]));

// ISR routine for zynswitches & zyncoders
void zyncoder_mcp23017_ISR(struct wiringPiNodeStruct *wpns, uint16_t base_pin, uint8_t bank);

#endif

//-----------------------------------------------------------------------------
// MCP23008 stuff
//-----------------------------------------------------------------------------
#ifdef MCP23008_ENCODERS

//Switches Polling Thread (should be avoided!)
pthread_t init_poll_zynswitches();

#endif

//-----------------------------------------------------------------------------
// Zynswitch data & functions
//-----------------------------------------------------------------------------

#define MAX_NUM_ZYNSWITCHES 36

typedef struct zynswitch_st {
	uint8_t enabled;            // 1 if switch enabled
	uint8_t pin;                // Index of GPI switch attached to
	unsigned long tsus;         // Absolute time in ms when switch closed
	unsigned int dtus;          // Duration of switch closure in us
	// note that this status is like the pin_[ab]_last_state for the zyncoders
	uint8_t status;             // Current switch state [0:closed, 1:open]

	midi_event_t midi_event;    // MIDI event type triggered by switch
	int last_cvgate_note;       // MIDI note last triggered by cv/gate [0..127]
} zynswitch_t;

zynswitch_t zynswitches[MAX_NUM_ZYNSWITCHES];

/** @brief  Reset all zynswitches to default state
*/
void reset_zynswitches();

/** @brief Get quantity of enabled switches
*   @retval int Quantity of enabled switches
*/
int get_num_zynswitches();

/** @brief  Get highest index of enabled switches
*   @retval int Index of last enabled switch
*   @todo   Does this have any use or benefit?
*/
int get_last_zynswitch_index();

/** @brief  Configure switch
*   @param  zynswitch Index of switch
*   @param  pin GPI pin assigned to switch
*   @retval int 1 on success, 0 on failure
*/
int setup_zynswitch(uint8_t zynswitch, uint8_t pin);

/** @brief  Assign MIDI event to be triggered by switch
*   @param  zynswitch Index of switch [0..MAX_NUM_ZYNSWITCHES]
*   @param  midi_evt MIDI command type [0..127]
*   @param  midi_chan MIDI channel [0..15]
*   @param  midi_num MIDI CC number [0..127]
*   @param  midi_val MIDI value [0..127]
*   @retval int 1 on success, 0 on failure
*/
int setup_zynswitch_midi(uint8_t zynswitch, midi_event_type midi_evt, uint8_t midi_chan, uint8_t midi_num, uint8_t midi_val);

/** @brief  Get duration of switch closure
*   @param  zynswitch Index of switch [0..MAX_NUM_ZYNSWITCHES]
*   @param  long_dtus Duration in us of long press afterwhich switch is deemed open even if still closed
*   @retval unsigned int Duration of switch closure in ms or 0 if switch open
*   @note   After period defined by long_dtus a switch open state is identified to allow long switch closure to trigger event
*/
unsigned int get_zynswitch(uint8_t zynswitch, unsigned int long_dtus);

/** @brief  Get index of next switch that is (or has recently been) closed
*   @param  zynswitch Index of switch from which to start search [0..MAX_NUM_ZYNSWITCHES]
*   @retval int Index of next closed switch or -1 if all open
*/
int get_next_pending_zynswitch(uint8_t zynswitch);

//-----------------------------------------------------------------------------
// Zyncoder data (Incremental Rotary Encoders)
//-----------------------------------------------------------------------------

#define MAX_NUM_ZYNCODERS 4

typedef struct zyncoder_st {
	uint8_t enabled;            // 1 to enable encoder
	int32_t min_value;          // Upper range value
	int32_t max_value;          // Lower range value
	int32_t step;               // Size of change in value for each detent of encoder
	uint8_t inv;                // 1 to invert range
	int32_t value;              // Current encdoder value [min_value..max_value]
	uint8_t value_flag;         // 1 if value changed since last read
	int8_t zpot_i;              // Zynpot index assigned to this encoder

	// Next fields are zyncoder-specific
	uint8_t pin_a;              // Data GPI
	uint8_t pin_b;              // Clock GPI
	uint8_t pin_a_last_state;   // Value of data GPI before current read
	uint8_t pin_b_last_state;   // Value of clock GPI before current read
	uint8_t code;               // Quadrant encoder algorithm current value
	uint8_t count;              // Quadrant encoder algorithm current count
	unsigned long tsus;         // Absolute time of last encoder change in microseconds
} zyncoder_t;
zyncoder_t zyncoders[MAX_NUM_ZYNCODERS];

//-----------------------------------------------------------------------------
// Zyncoder's zynpot API
//-----------------------------------------------------------------------------

/** @brief  Reset all encoders to default configuration
*/
void reset_zyncoders();

/** @brief  Get quantity of enabled encoders
*   @retval int Quantity of enabled encoders [0..MAX_NUM_ZYNCODERS]
*/
int get_num_zyncoders();

/** @brief  Assign GPI pins to encoder
*   @param  encoder Index of encoder [0..MAX_NUM_ZYNCODERS]
*   @param  pin_a GPI assigned to encoder clock pin
*   @param  pin_b GPI assigned to encoder data pin
*   @retval int 1 on success, 0 on failure
*/
int setup_zyncoder(uint8_t encoder, uint8_t pin_a, uint8_t pin_b);

/** @brief  Configure encoder
*   @param  encoder Index of encoder [0..MAX_NUM_ZYNCODERS]
*   @param  min_value Lower bound of value range
*   @param  max_value Upper bound of value range
*   @param  value Current value to assign to encoder
*   @param  step Size of change in value for each detent of encoder
*   @retval int 1 on success, 0 on failure
*/
int setup_rangescale_zyncoder(uint8_t encoder, int32_t min_value, int32_t max_value, int32_t value, int32_t step);

/** @brief  Get current value
*   @param  encoder Index of encoder [0..MAX_NUM_ZYNCODERS]
*   @retval int32_t Current value
*/
int32_t get_value_zyncoder(uint8_t encoder);

/** @brief  Check if value has changed since last read
*   @param  encoder Index of encoder [0..MAX_NUM_ZYNCODERS]
*   @retval uint8_t 1 if value changed since last read.
*/
uint8_t get_value_flag_zyncoder(uint8_t encoder);

/** @brief  Set value
*   @param  encoder Index of encoder
*   @param  value New value
*   @retval int 1 on success. 0 on failure
*/
int set_value_zyncoder(uint8_t encoder, int32_t value);

//-----------------------------------------------------------------------------
