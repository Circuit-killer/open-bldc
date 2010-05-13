/*
 * Open-BLDC - Open BrushLess DC Motor Controller
 * Copyright (C) 2010 by Piotr Esden-Tempski <piotr@esden.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "types.h"

#include "led.h"
#include "comm_tim.h"
#include "comm_process.h"

#include "sensor_process.h"

struct comm_process_state {
	volatile bool rising;
	int pwm_count;
	bool closed_loop;
	bool recalculated_comm_time;
	bool just_started;
	u32 prev_phase_voltage;
	u32 prev_prev_phase_voltage;
};

struct comm_process_state comm_process_state;
struct comm_params comm_params;
u32 new_cycle_time;

void comm_process_init(void)
{
	comm_process_state.rising = true;
	comm_process_state.pwm_count = 0;
	comm_process_state.closed_loop = false;
	comm_process_state.recalculated_comm_time = false;
	comm_process_state.just_started = true;
	comm_process_state.prev_phase_voltage = 0;

	comm_params.spark_advance = 0;
	comm_params.direct_cutoff = 10000;
	comm_params.direct_cutoff_slope = 100;
	comm_params.iir = 7;
	comm_params.hold_off = 2;
}

void comm_process_reset(void)
{
	comm_process_state.pwm_count = 0;
	comm_process_state.recalculated_comm_time = false;
}

void comm_process_config(bool rising){
	comm_process_state.rising = rising;
}

void comm_process_config_and_reset(bool rising){
	comm_process_state.rising = rising;
	if(rising){
		comm_process_state.prev_prev_phase_voltage = 65535;
		comm_process_state.prev_phase_voltage = 65535;
		sensors.phase_voltage = 65535;
	}else{
		comm_process_state.prev_prev_phase_voltage = 0;
		comm_process_state.prev_phase_voltage = 0;
		sensors.phase_voltage = 0;
	}
	comm_process_state.pwm_count = 0;
	comm_process_state.recalculated_comm_time = false;
	LED_RED_OFF();
}

void comm_process_closed_loop_on(void)
{
	comm_process_state.closed_loop = true;
	comm_process_state.just_started = true;
}

void comm_process_closed_loop_off(void)
{
	comm_process_state.closed_loop = false;
}

//void comm_process_calc_next_comm_for_rising(void)
//{
//	u32 half_cycle_time = comm_tim_data.curr_time -
//		comm_tim_data.last_capture_time;
//	new_cycle_time = half_cycle_time * 2;
//
//	if(!comm_process_state.recalculated_comm_time){
//		comm_process_state.recalculated_comm_time = true;
//		//if(half_cycle_time > 300){
//			if(new_cycle_time >
//				(comm_tim_data.freq + comm_params.direct_cutoff)){
//				new_cycle_time = comm_tim_data.freq +
//					comm_params.direct_cutoff_slope;
//			}else if(new_cycle_time <
//				(comm_tim_data.freq - comm_params.direct_cutoff)){
//				new_cycle_time = comm_tim_data.freq -
//					comm_params.direct_cutoff_slope;
//			}
//
//			if(comm_process_state.closed_loop){
//				comm_tim_data.freq = ((comm_tim_data.freq * comm_params.iir) +
//						new_cycle_time) / (comm_params.iir + 1);
//				comm_tim_update_freq();
//			}
//			//}
//		gpc_register_touched(10);
//	}
//}
//
//void comm_process_calc_next_comm_for_falling(void)
//{
//	u32 half_cycle_time = comm_tim_data.curr_time -
//		comm_tim_data.last_capture_time;
//	new_cycle_time = half_cycle_time * 2;
//
//	if(!comm_process_state.recalculated_comm_time){
//		comm_process_state.recalculated_comm_time = true;
//		//if(half_cycle_time > 300){
//			if(new_cycle_time <
//				(comm_tim_data.freq - comm_params.direct_cutoff)){
//				new_cycle_time = comm_tim_data.freq -
//					comm_params.direct_cutoff_slope;
//			}else if(new_cycle_time >
//				(comm_tim_data.freq + comm_params.direct_cutoff)){
//				new_cycle_time = comm_tim_data.freq +
//					comm_params.direct_cutoff_slope;
//			}
//
//			if(comm_process_state.closed_loop){
//				comm_tim_data.freq = ((comm_tim_data.freq * comm_params.iir) +
//						new_cycle_time) / (comm_params.iir + 1);
//				comm_tim_update_freq();
//			}
//			//}
//	}
//}

void comm_process_calc_next_comm(void)
{
	u32 old_cycle_time = comm_tim_data.freq;
	u32 half_cycle_time = comm_tim_data.curr_time -
		comm_tim_data.last_capture_time;
	new_cycle_time = half_cycle_time * 2;

	if(!comm_process_state.recalculated_comm_time){
		comm_process_state.recalculated_comm_time = true;
		if((half_cycle_time > 500) &&
			(half_cycle_time < (0xFFFF - 500))){
			comm_process_state.just_started = false;
			if(new_cycle_time >
				(old_cycle_time + comm_params.direct_cutoff)){
				new_cycle_time = old_cycle_time +
					comm_params.direct_cutoff_slope;
			}else if(new_cycle_time <
				(old_cycle_time - comm_params.direct_cutoff)){
				new_cycle_time = old_cycle_time -
					comm_params.direct_cutoff_slope;
			}

			if(comm_process_state.closed_loop){
				new_cycle_time = ((old_cycle_time * comm_params.iir) +
						(new_cycle_time + comm_params.spark_advance)) / (comm_params.iir + 1);
				comm_tim_data.freq = new_cycle_time;
				comm_tim_update_freq();
			}
		}else{
			if((half_cycle_time <= 500) &&
				comm_process_state.just_started){
				comm_tim_data.freq -= 500;
			}else if(comm_process_state.closed_loop){
				comm_tim_off();
			}
		}
		gpc_register_touched(10);
	}
}

void run_comm_process(void)
{
	LED_RED_ON();
	/*
	if(comm_process_state.pwm_count < comm_process_state.hold_off){
		comm_process_state.pwm_count++;
		return;
	}

	if(sensors.phase_voltage > 1000){
		if(comm_process_state.rising){
			if(sensors.phase_voltage >= sensors.half_battery_voltage){
				comm_process_calc_next_comm();
				LED_ORANGE_OFF();
			}else{
				LED_ORANGE_ON();
			}
		}else{
			if(sensors.phase_voltage <= sensors.half_battery_voltage){
				comm_process_calc_next_comm();
				LED_ORANGE_ON();
			}else{
				LED_ORANGE_OFF();
			}
		}
	}

	*/

	if(comm_process_state.pwm_count < comm_params.hold_off){
		comm_process_state.pwm_count++;
		comm_process_state.prev_prev_phase_voltage = comm_process_state.prev_phase_voltage;
		comm_process_state.prev_phase_voltage = sensors.phase_voltage;
		LED_ORANGE_OFF();
		LED_RED_OFF();
		return;
	}

	if((sensors.phase_voltage > 500) &&
		(sensors.phase_voltage < (0xFFF - 500))){
		if(comm_process_state.rising){
			if((comm_process_state.prev_prev_phase_voltage < comm_process_state.prev_phase_voltage) &&
				(comm_process_state.prev_phase_voltage < sensors.half_battery_voltage) &&
				(sensors.phase_voltage >= sensors.half_battery_voltage)){
				//comm_process_calc_next_comm();
				LED_ORANGE_ON();
			}else{
				LED_ORANGE_OFF();
			}
		}else{
			if((comm_process_state.prev_prev_phase_voltage > comm_process_state.prev_phase_voltage) &&
				(comm_process_state.prev_phase_voltage > sensors.half_battery_voltage) &&
				(sensors.phase_voltage <= sensors.half_battery_voltage)){
				//comm_process_calc_next_comm();
				LED_ORANGE_ON();
			}else{
				LED_ORANGE_OFF();
			}
		}
	}else{
		LED_ORANGE_OFF();
	}

	comm_process_state.prev_prev_phase_voltage = comm_process_state.prev_phase_voltage;
	comm_process_state.prev_phase_voltage = sensors.phase_voltage;

	LED_RED_OFF();
}