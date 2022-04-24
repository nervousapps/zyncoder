/*
 * ******************************************************************
 * ZYNTHIAN PROJECT: Zyncoder I2C HWC Library
 *
 * Library for interfacing Rotary Encoders & Switches connected
 * to RBPi via I2C. Includes an
 * emulator mode to ease developing.
 *
 * Copyright (C) 2015-2018 Fernando Moyano <jofemodo@zynthian.org>
 * Copyright (C) 2019 Brian Walton <brian@riban.co.uk>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <wiringPi.h>

#include "zyncoder_i2c.h"
#include "zynmidirouter.h"

#if defined(NSYNTH)
	#include <fcntl.h>
	#include <sys/ioctl.h>
	#include <linux/i2c-dev.h>
#else
	#include <wiringPiI2C.h>
#endif

#define DEBUG

#if !defined(MCP23017_INTA_PIN)
		#define INTERRUPT_PIN 7
#else
    #define INTERRUPT_PIN MCP23017_INTA_PIN
#endif
//-----------------------------------------------------------------------------
// Library Initialisation
//-----------------------------------------------------------------------------

/** Initialise zyncoder library */
int init_zynlib() {
	if (!init_zyncoder()) return 0;
	if (!init_zynmidirouter()) return 0;
	return 1;
}

/** Destruct zyncoder library */
int end_zynlib() {
	if (!end_zynmidirouter()) return 0;
	if (!end_zyncoder()) return 0;
	return 1;
}

//-----------------------------------------------------------------------------
// Zyncoder Library Initialisation
//-----------------------------------------------------------------------------

/** @brief  Initialises encoders and switches
*   @retval int 1 on success, 0 on fail
*/
int init_zyncoder() {
	int i;
	for (i=0;i<MAX_NUM_ZYNSWITCHES;i++) {
		zynswitches[i].enabled=0;
		zynswitches[i].midi_cc=0;
	}
	for (i=0;i<MAX_NUM_ZYNCODERS;i++) {
		zyncoders[i].enabled=0;
	}

#if !defined(NSYNTH)
	wiringPiSetup();
	hwci2c_fd = wiringPiI2CSetup(HWC_ADDR);
	wiringPiI2CWriteReg8(hwci2c_fd, 0, 0); // Reset HWC
	wiringPiISR(INTERRUPT_PIN, INT_EDGE_FALLING, handleRibanHwc);
#else
	wiringPiSetup();

	// Initialize buttons
	for(int i=0; i<4; i++) {
		pinMode(PATCH_WP_PINS[i] , INPUT);
		pullUpDnControl(PATCH_WP_PINS[i] , PUD_UP);
	}

	wiringPiISR(PATCH_WP_PINS[0] , INT_EDGE_BOTH, &button0Handler);
	wiringPiISR(PATCH_WP_PINS[1] , INT_EDGE_BOTH, &button1Handler);
	wiringPiISR(PATCH_WP_PINS[2] , INT_EDGE_BOTH, &button2Handler);
	wiringPiISR(PATCH_WP_PINS[3] , INT_EDGE_BOTH, &button3Handler);

	i2c = open("/dev/i2c-1", O_RDWR);
	if(i2c < 0){
		printf("I2C opening fails !");
		return;
	}

	pthread_t inputsThreadId ;
	pthread_create (&inputsThreadId, NULL, &readNsInputsThread, NULL) ;
#endif
	return 1;
}

/** @brief  Destroy encoders and switches
*   @retval int 1 on success, 0 on fail
*/
int end_zyncoder() {
	return 1;
}

//-----------------------------------------------------------------------------
// GPIO Switches
//-----------------------------------------------------------------------------

/** @brief  Update the status (value) of a switch
*   @param  i Index of switch to update
*   @param  status New status (value) of switch
*   @note   Triggers any configured switch events. Does nothing if switch disabled or status not changed.
*   @note   Updates switch close time / switch release duration.
*/
void update_zynswitch(uint8_t i, uint8_t status) {
	struct zynswitch_st *zynswitch = zynswitches + i;
	if (zynswitch->enabled==0) return;
	if (status==zynswitch->status) return;
	zynswitch->status=status;

	if (zynswitch->midi_cc>0) {
		uint8_t val=0;
		if (status==0) val=127;
		//Send MIDI event to engines and ouput (ZMOPS)
		internal_send_ccontrol_change(zynswitch->midi_chan, zynswitch->midi_cc, val);
		//Update zyncoders
		midi_event_zyncoders(zynswitch->midi_chan, zynswitch->midi_cc, val);
		//Send MIDI event to UI
		write_zynmidi_ccontrol_change(zynswitch->midi_chan, zynswitch->midi_cc, val);
	}

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	unsigned long int tsus=ts.tv_sec*1000000 + ts.tv_nsec/1000;

    // Switch active 0 - detect switch press and set press time (tsus) or detect release and set press duration (dtus)
	if (zynswitch->status==1) {
        // Switch released
		if (zynswitch->tsus>0) {
			unsigned int dtus=tsus-zynswitch->tsus;
			zynswitch->tsus=0;
			if (dtus<1000) return;
			zynswitch->dtus=dtus;
		}
	} else zynswitch->tsus=tsus;
}

//-----------------------------------------------------------------------------

/** @brief  Configure switch
*   @param  i Vitrual switch index
*   @param  index Physical (I2C) switch index
*   @retval zynswitch_st* Pointer to the switch structure (null if invalid switch virtual index)
*/
struct zynswitch_st *setup_zynswitch(uint8_t i, uint8_t index) {
	if (i >= MAX_NUM_ZYNSWITCHES) {
		printf("Zyncoder: Maximum number of zynswitches exceeded: %d\n", MAX_NUM_ZYNSWITCHES);
		return 0;
	}
	struct zynswitch_st *zynswitch = zynswitches + i;
	zynswitch->enabled = 1;
	zynswitch->index = index + 64; // First switch is at I2C register 64
	zynswitch->tsus = 0;
	zynswitch->dtus = 0;
	zynswitch->status = 1; // Switches are active low
    return zynswitch;
}

/** @brief  Configure MIDI event to trigger for switch press (release)
*   @param  i Virtual switch index
*   @param  midi_chan MIDI channel for event
*   @param  midi_cc MIDI control change for event
*   @retval int 1 on success, 0 on failure
*/
int setup_zynswitch_midi(uint8_t i, uint8_t midi_chan, uint8_t midi_cc) {
	if (i >= MAX_NUM_ZYNSWITCHES) {
		printf("Zyncoder: Maximum number of zynswitches exceeded: %d\n", MAX_NUM_ZYNSWITCHES);
		return 0;
	}

	struct zynswitch_st *zynswitch = zynswitches + i;
	zynswitch->midi_chan = midi_chan;
	zynswitch->midi_cc = midi_cc;

	return 1;
}

/** @brief  Get the duration of last switch press and release
*   @param  i Virtual switch index
*   @param  long_dtus Timeout for long press (us)
*   @retval unsigned int Duration of last switch press in us or zero if switch not pressed and released
*   @note   Resets duration
*/
unsigned int get_zynswitch_dtus(uint8_t i, unsigned int long_dtus) {
	if (i >= MAX_NUM_ZYNSWITCHES) return 0;
	unsigned int dtus=zynswitches[i].dtus;
	if(dtus>0) {
		zynswitches[i].dtus=0;
		return dtus;
	}
	else if (zynswitches[i].tsus>0) {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		dtus=ts.tv_sec*1000000 + ts.tv_nsec/1000 - zynswitches[i].tsus;
		if (dtus>long_dtus) {
			zynswitches[i].tsus=0;
			return dtus;
		}
	}
	return 0;
}

/** @brief  Get the duration of last switch press and release
*   @param  i Virtual switch index
*   @retval unsigned int Duration of last switch press in us or zero if switch not pressed and released
*   @note   Resets duration
*/
unsigned int get_zynswitch(uint8_t i, unsigned int long_dtus) {
	return get_zynswitch_dtus(i, long_dtus);
}

//-----------------------------------------------------------------------------
// Generic Rotary Encoders
//-----------------------------------------------------------------------------

/** @brief Set encoder value from MIDI event
*   @param  midi_chan MIDI channel
*   @param  midi_ctrl MIDI controller
*   @param  val Value to set encoder to
*/
void midi_event_zyncoders(uint8_t midi_chan, uint8_t midi_ctrl, uint8_t val) {
	//Update zyncoder value => TODO Optimize this fragment!!!
	int j;
	for (j=0;j<MAX_NUM_ZYNCODERS;j++) {
		if (zyncoders[j].enabled && zyncoders[j].midi_chan==midi_chan && zyncoders[j].midi_ctrl==midi_ctrl) {
			zyncoders[j].value=val;
			//fprintf (stdout, "ZynMidiRouter: MIDI CC (%x, %x) => UI",midi_chan,midi_ctrl);
		}
	}
}

/** @brief  Send MIDI CC or OSC events from encoder value
*   @param  i Index of encoder
*/
void send_zyncoder(uint8_t i) {
	if (i>=MAX_NUM_ZYNCODERS) return;
	struct zyncoder_st *zyncoder = zyncoders + i;
	if (zyncoder->enabled==0) return;
	if (zyncoder->midi_ctrl>0) {
		//Send to MIDI output
		internal_send_ccontrol_change(zyncoder->midi_chan,zyncoder->midi_ctrl,zyncoder->value);
		//Send to MIDI controller feedback => TODO: Reverse Mapping!!
		ctrlfb_send_ccontrol_change(zyncoder->midi_chan,zyncoder->midi_ctrl,zyncoder->value);
		//printf("SEND MIDI CHAN %d, CTRL %d = %d\n",zyncoder->midi_chan,zyncoder->midi_ctrl,zyncoder->value);
	} else if (zyncoder->osc_lo_addr!=NULL && zyncoder->osc_path[0]) {
		if (zyncoder->step >= 8) {
			if (zyncoder->value>=64) {
				lo_send(zyncoder->osc_lo_addr,zyncoder->osc_path, "T");
				//printf("SEND OSC %s => T\n",zyncoder->osc_path);
			} else {
				lo_send(zyncoder->osc_lo_addr,zyncoder->osc_path, "F");
				//printf("SEND OSC %s => F\n",zyncoder->osc_path);
			}
		} else {
			lo_send(zyncoder->osc_lo_addr,zyncoder->osc_path, "i",zyncoder->value);
			//printf("SEND OSC %s => %d\n",zyncoder->osc_path,zyncoder->value);
		}
	}
}

//-----------------------------------------------------------------------------

/** @brief  Configure rotary encoder
*   @param  i Index of encoder
*   @param  pin_a GPIO of encoder clock or physical (I2C) encoder index
*   @param  pin_b GPIO of encoder data (not used by I2C)
*   @param  midi_chan MIDI channelf of control change
*   @param  midi_ctrl MIDI control change
*   @param  osc_path OSC path
*   @param  value Inital value of encoder
*   @param  max_value Maximum permissible value
*   @param  step Value increment size per encoder click
*   @retval zyncoder_st* Pointer to encoder structure
*/
struct zyncoder_st *setup_zyncoder(uint8_t i, uint8_t pin_a, uint8_t pin_b, uint8_t midi_chan, uint8_t midi_ctrl, char *osc_path, unsigned int value, unsigned int max_value, unsigned int step) {
	if (i > MAX_NUM_ZYNCODERS) {
		printf("Zyncoder: Maximum number of zyncoders exceded: %d\n", MAX_NUM_ZYNCODERS);
		return NULL;
	}
#ifdef DEBUG
	printf("Set up encoder i=%d, pin_a=%d, pin_b=%d, midich=%d, midictl=%d, oscpath=%s, value=%d, maxval=%d, step=%d\n",
          i, pin_a, pin_b, midi_chan, midi_ctrl, osc_path, value, max_value, step);
#endif // DEBUG

	struct zyncoder_st *zyncoder = zyncoders + i;
	if (midi_chan>15) midi_chan=0;
	if (midi_ctrl>127) midi_ctrl=1;
	if (value>max_value) value=max_value;
	zyncoder->midi_chan = midi_chan;
	zyncoder->midi_ctrl = midi_ctrl;
	zyncoder->index = pin_a + 114; // I2C encoders start at register 115
	zyncoder->step = step;

	if (osc_path) {
		char *osc_port_str=strtok(osc_path,":");
		zyncoder->osc_port=atoi(osc_port_str);
		if (zyncoder->osc_port>0) {
			zyncoder->osc_lo_addr=lo_address_new(NULL,osc_port_str);
			strcpy(zyncoder->osc_path,strtok(NULL,":"));
		}
		else zyncoder->osc_path[0]=0;
	} else zyncoder->osc_path[0]=0;

    zyncoder->value = (value < max_value)?value:max_value;
    zyncoder->max_value = max_value;
    zyncoder->enabled = 1;

	return zyncoder;
}

/** @brief  Get rotary encoder value
*   @param  i Index of encoder
*   @retval unsigned int Encoder value
*/
unsigned int get_value_zyncoder(uint8_t i) {
	if (i >= MAX_NUM_ZYNCODERS) return 0;
	return zyncoders[i].value;
}

/** @brief  Set absolute value of rotary encoder
*   @param  i Encoder index
*   @param  v Value
*   @param  send Send MIDI CC and OSC updates
*/
void set_value_zyncoder(uint8_t i, unsigned int v, int send) {
	if (i >= MAX_NUM_ZYNCODERS) return;
	struct zyncoder_st *zyncoder = zyncoders + i;
	if (zyncoder->enabled==0) return;

    if(zyncoder->step)
        v *= zyncoder->step;
    if(v > zyncoder->max_value)
        v = zyncoder->max_value;
    zyncoder->value = v;

	if (send) send_zyncoder(i);
}

/** Called when an interrupt signal detected from riban HWC.
    Interrupt indicates a change has occured on HWC hence there is data to read.
    Must read one byte from HWC register 0 to detect the control that has changed then read that control's value.
    (Controller index starts at 1. '0' means there are no changes since last read.)
    Control value may be absolute (e.g. potentiometer or switch) or relative from last read (e.g. rotary encoder).
    Interrupt remains asserted until all changed values are read.
    We use zyncoder_st::pin_a to hold HWC enoder index.
    We use zynswitch_st::pin to hold HWC switch index.
*/
/** @brief  Handle I2C hardware controller interrupt signal
*   @note   Reads all changed controls, updates switches and encoders and triggers events
*/
void handleRibanHwc() {
    //loop until all HWC changes are read
    int i;
    uint8_t reg;
    while(reg = wiringPiI2CRead(hwci2c_fd)) {
        int16_t nValue = wiringPiI2CReadReg16(hwci2c_fd, reg);
        for(i=0; i<MAX_NUM_ZYNCODERS; i++) {
            struct zyncoder_st *zyncoder = zyncoders + i;
            if(zyncoder->enabled==0 || zyncoder->index != reg)
                continue;
            if(zyncoder->step)
                nValue *= ZYNCODER_TICKS_PER_RETENT * zyncoder->step;
            nValue += zyncoder->value;
            if(nValue < 0)
                nValue = 0;
            if(nValue > zyncoder->max_value)
                nValue = zyncoder->max_value;
            zyncoder->value = nValue;
            send_zyncoder(i);
            break;
        }
        for(i=0; i<MAX_NUM_ZYNSWITCHES; i++) {
            struct zynswitch_st *zynswitch = zynswitches + i;
            if(zynswitch->enabled == 0 || zynswitch->index != reg)
                continue;
            update_zynswitch(i, nValue?0:1); // Have to invert switch value because zyncoder uses active low switch values
            break;
        }
    }
}

/** Nsynth hardware **/
void readNsInputsThread(){
	while(1) {
		if(i2c < 0){
			continue;
		}
		// Switch to the MCU address.
		if(ioctl(i2c, I2C_SLAVE, HWC_ADDR) < 0){
			continue;
		}

		// Issue an SMBus read from address 0.
		uint8_t buf[1] = {0};
		if(write(i2c, buf, 1) != 1){
			continue;
		}

		// Read the whole inputs message from the MCU.
		struct InputsMessage message;
		if(read(i2c, &message, sizeof(message)) != sizeof(message)){
			continue;
		}

		// Check the checksum.
		uint32_t *src = (uint32_t *)(&message);
		uint32_t chk = 0xaa55aa55;
		chk += src[0];
		chk += src[1];
		chk += src[2];
		if(chk != message.chk){
			continue;
		}

		int j = 0;
		for(int i=0; i<4; i++) {
			j = i;
			if (i == 1){
				j = 2;
			}
			if (i == 2){
				j = 1;
			}
			struct zyncoder_st *zyncoder = zyncoders + j;
			int8_t delta = message.rotaries[i] - lastInputsMessage.rotaries[i];
			int16_t nValue = zyncoder->value;
			if (delta != 0){
				if(delta > 0)
					nValue += 1;
				if(delta < 0)
					nValue -= 1;
				if(nValue < 0)
					nValue = 0;
				if(nValue > zyncoder->max_value)
					nValue = zyncoder->max_value;
				zyncoder->value = nValue;
				send_zyncoder(i);
			}
		}

		for(int i=0; i<6; i++) {
			struct zyncoder_st *zyncoder = zyncoders + i;
			if (message.potentiometers[i]/2 != lastInputsMessage.potentiometers[i]/2){
				write_zynmidi_ccontrol_change(1, i+60, message.potentiometers[i]/2);
			}
		}

		for(int i=0; i<2; i++) {
			if (message.touch[i] != lastInputsMessage.touch[i] && message.touch[i] != 255){
				printf("############### \n");
				printf("%d", message.touch[i]);
				printf("############### \n");
			}
		}

		lastInputsMessage = message;
		time_sleep(0.05);
	}
}

void button0Handler() {
	int key_state = digitalRead(PATCH_WP_PINS[0]);
	if (previous_key_state0 != key_state) {
		previous_key_state0 = key_state;
		if (key_state == HIGH) {
			/* Key released */
			printf("Key 0 released");
			update_zynswitch(0, key_state);
		}
		else {
			/* Key pressed */
			printf("Key 0 pressed");
			update_zynswitch(0, key_state);
		}
	}
}

void button1Handler() {
	int key_state = digitalRead(PATCH_WP_PINS[1]);
	if (previous_key_state1 != key_state) {
		previous_key_state1 = key_state;
		if (key_state == HIGH) {
			/* Key released */
			printf("Key 1 released");
			update_zynswitch(1, key_state);
		}
		else {
			/* Key pressed */
			printf("Key 1 pressed");
			update_zynswitch(1, key_state);
		}
	}
}

void button2Handler() {
	int key_state = digitalRead(PATCH_WP_PINS[2]);
	if (previous_key_state2 != key_state) {
		previous_key_state2 = key_state;
		if (key_state == HIGH) {
			/* Key released */
			printf("Key 2 released");
			update_zynswitch(2, key_state);
		}
		else {
			/* Key pressed */
			printf("Key 2 pressed");
			update_zynswitch(2, key_state);
		}
	}
}

void button3Handler() {
	int key_state = digitalRead(PATCH_WP_PINS[3]);
	if (previous_key_state3 != key_state) {
		previous_key_state3 = key_state;
		if (key_state == HIGH) {
			/* Key released */
			printf("Key 3 released");
			update_zynswitch(3, key_state);
		}
		else {
			/* Key pressed */
			printf("Key 3 pressed");
			update_zynswitch(3, key_state);
		}
	}
}
