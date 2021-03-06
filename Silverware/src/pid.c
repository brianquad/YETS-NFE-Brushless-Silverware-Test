/*
The MIT License (MIT)

Copyright (c) 2016 silverx

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdbool.h>
#include <stdlib.h>
#include "pid.h"
#include "util.h"
#include "config.h"
#include "led.h"
#include "defines.h"

#include <math.h>


//**************************************************PIDS*************************************************
//*******************************************************************************************************

//**************************ADVANCED PID CONTROLLER - WITH PROFILE SWITCHING ON AUX SWITCH PIDPROFILE******************************* 
// GENERAL SUMMARY OF THIS FEATURE:
// stickAccelerator and stickTransition are a more detailed version of the traditional D term setpoint weight and transition variables that you may be familiar with in other firmwares.
// The difference here is that we name the D term setpoint weight "Stick Accelerator" because it's actual function is to accelerate the response of the pid controller to stick inputs.
// Another difference is that negative stick transitions are possible meaning that you can have a higher stick acceleration near center stick which fades to a lower stick acceleration at
// full stick throws should you desire to see what that feels like.  Traditionally we are only used to being able to transition from a low setpoint to a higher one.
// The final differences are that you can adjust each axis independently and also set up two seperate profiles so that you can switch "feels" in flight with the PIDPROFILE aux
// channel selection set up in the receiver section of config.h
//
//HOW TO USE THIS FEATURE:
// Safe values for stickAccelerator are from 0 to about 2.5 where 0 represents a "MEASUREMENT" based D term calculation and is the traditional Silverware PID controller, and a
// a value of 1 represents an "ERROR" based D term calculation.  Values above 1 add even more acceleration but be reasonable and keep this below about 2.5.
 
// Range of acceptable values for stickTransition are from -1 to 1.  Do not input a value outside of this range.  When stick transition is 0 - no stick transition will take place
// and stick acceleration will remain constant regardless of stick position.  Positive values up to 1 will represent a transition where stick acceleration at it's maximum at full
// stick deflection and is reduced by whatever percentage you enter here at stick center.  For example accelerator at 1 and transition at .3 means that there will be 30% reduction 
// of acceleration at stick center, and acceleration strength of 1 at full stick.

//pid profile A						 Roll  PITCH  YAW
float stickAcceleratorProfileA[3] = { 0.0 , 0.0 , 0.0};           //keep values between 0 and 2.5
float stickTransitionProfileA[3]  = { 0.0 , 0.0 , 0.0};           //keep values between -1 and 1

//pid profile B						 Roll  PITCH  YAW
float stickAcceleratorProfileB[3] = { 1.0 , 1.0 , 1.0};           //keep values between 0 and 2.5
float stickTransitionProfileB[3]  = { 0.0 , 0.0 , 0.0};           //keep values between -1 and 1 

//*********************************Saved Initial PIDs****************************************
float pidkp_init[PIDNUMBER] = { 0, 0, 0 };
float pidki_init[PIDNUMBER] = { 0, 0, 0 };
float pidkd_init[PIDNUMBER] = { 0, 0, 0 };

//*********************************************Setpoint Weight*******************************************
// "setpoint weighting" 0.0 - 1.0 where 1.0 = normal pid
#define ENABLE_SETPOINT_WEIGHTING
//            Roll   Pitch   Yaw
float b[3] = { 1.0 , 1.0 , 1.0};


/// output limit			
const float outlimit[PIDNUMBER] = { 0.6 , 0.6 , 0.3 };

// limit of integral term (abs)
const float integrallimit[PIDNUMBER] = { 0.6 , 0.6 , 0.3 };

//#define RECTANGULAR_RULE_INTEGRAL
#define MIDPOINT_RULE_INTEGRAL
//#define SIMPSON_RULE_INTEGRAL

//#define ANTI_WINDUP_DISABLE


//*******************************************DO NOT CHANGE************************************************
// non changable things below

// multiplier for pids at 3V - for PID_VOLTAGE_COMPENSATION - default 1.33f
#define PID_VC_FACTOR 1.33f

// --------------------------- DUAL PIDS CODE -----------------
//
// first PID set (do not change)
//
float pidkp1[PIDNUMBER] = PIDKP1;
float pidki1[PIDNUMBER] = PIDKI1;	
float pidkd1[PIDNUMBER] = PIDKD1;	

//
// second PID set (do not change)
//
float pidkp2[PIDNUMBER] = PIDKP2;
float pidki2[PIDNUMBER] = PIDKI2;	
float pidkd2[PIDNUMBER] = PIDKD2;	


// working arrays - do not change
float pidkp[PIDNUMBER] = { 0 , 0 , 0 }; 
float pidki[PIDNUMBER] = { 0 , 0 , 0 };	
float pidkd[PIDNUMBER] = { 0 , 0 , 0 };	

//float * pids_array[3] = {pidkp, pidki, pidkd}; //disabled for dual PIDs code
float * pids_array[3] = {pidkp1, pidki1, pidkd1};	//dual PIDs code (original array filled with PID set1)
float * pids_array2[3] = {pidkp2, pidki2, pidkd2}; //dual PIDs code (set2)
int number_of_increments[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
int current_pid_axis = 0;
int current_pid_term = 0;
float * current_pid_term_pointer = pidkp;
float * current_pid_term_pointer1 = pidkp1; // dual PIDs code
float * current_pid_term_pointer2 = pidkp2; // dual PIDs code

float ierror[PIDNUMBER] = { 0 , 0 , 0};	
float pidoutput[PIDNUMBER];
float setpoint[PIDNUMBER];
float v_compensation = 1.00;

#ifdef ANALOG_AUX_PIDS
int analog_aux_pids_adjusted = 0;
#endif


static float lasterror[PIDNUMBER];

extern float error[PIDNUMBER];
extern float setpoint[PIDNUMBER];
extern float looptime;
extern float gyro[3];
extern int onground;
extern float looptime;
extern int in_air;
extern char aux[AUXNUMBER];
extern float aux_analog[AUXNUMBER];
extern char aux_analogchange[AUXNUMBER];
extern float vbattfilt;


#ifdef NORMAL_DTERM
static float lastrate[PIDNUMBER];
#endif

#ifdef NEW_DTERM
static float lastratexx[PIDNUMBER][2];
#endif

#ifdef MAX_FLAT_LPF_DIFF_DTERM
static float lastratexx[PIDNUMBER][4];
#endif

#ifdef SIMPSON_RULE_INTEGRAL
static float lasterror2[PIDNUMBER];
#endif

float timefactor;

void apply_analog_aux_to_pids()
{
    // aux_analog channels are in range 0 to 1. Shift to 0 to 2 so we can zero out or double selected PID value.
    // only needs to perform multiplies when the channel in question has changed
    // only performance hit, then, is the true/false check on each enabled channel and the call to this function each pid loop

     // Roll PIDs
#ifdef ANALOG_R_P
    if (aux_analogchange[ANALOG_R_P]) {
        pidkp[0] = pidkp_init[0] * (aux_analog[ANALOG_R_P] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_R_I
    if (aux_analogchange[ANALOG_R_I]) {
        pidki[0] = pidki_init[0] * (aux_analog[ANALOG_R_I] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_R_D
    if (aux_analogchange[ANALOG_R_D]) {
        pidkd[0] = pidkd_init[0] * (aux_analog[ANALOG_R_D] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif

    // Pitch PIDs
#ifdef ANALOG_P_P
    if (aux_analogchange[ANALOG_P_P]) {
        pidkp[1] = pidkp_init[1] * (aux_analog[ANALOG_P_P] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_P_I
    if (aux_analogchange[ANALOG_P_I]) {
        pidki[1] = pidki_init[1] * (aux_analog[ANALOG_P_I] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_P_D
    if (aux_analogchange[ANALOG_P_D]) {
        pidkd[1] = pidkd_init[1] * (aux_analog[ANALOG_P_D] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif

    // Yaw PIDs
#ifdef ANALOG_Y_P
    if (aux_analogchange[ANALOG_Y_P]) {
        pidkp[2] = pidkp_init[2] * (aux_analog[ANALOG_Y_P] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_Y_I
    if (aux_analogchange[ANALOG_Y_I]) {
        pidki[2] = pidki_init[2] * (aux_analog[ANALOG_Y_I] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_Y_D
    if (aux_analogchange[ANALOG_Y_D]) {
        pidkd[2] = pidkd_init[2] * (aux_analog[ANALOG_Y_D] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif

    // Combined Roll and Pitch PIDs
#ifdef ANALOG_RP_P
    if (aux_analogchange[ANALOG_RP_P]) {
        pidkp[0] = pidkp_init[0] * (aux_analog[ANALOG_RP_P] + 0.5f);
        pidkp[1] = pidkp_init[1] * (aux_analog[ANALOG_RP_P] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_RP_I
    if (aux_analogchange[ANALOG_RP_I]) {
        pidki[0] = pidki_init[0] * (aux_analog[ANALOG_RP_I] + 0.5f);
        pidki[1] = pidki_init[1] * (aux_analog[ANALOG_RP_I] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
#ifdef ANALOG_RP_D
    if (aux_analogchange[ANALOG_RP_D]) {
        pidkd[0] = pidkd_init[0] * (aux_analog[ANALOG_RP_D] + 0.5f);
        pidkd[1] = pidkd_init[1] * (aux_analog[ANALOG_RP_D] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif

    // Combined Roll and Pitch P and D
#ifdef ANALOG_RP_PD
    if (aux_analogchange[ANALOG_RP_PD]) {
        pidkp[0] = pidkp_init[0] * (aux_analog[ANALOG_RP_PD] + 0.5f);
        pidkp[1] = pidkp_init[1] * (aux_analog[ANALOG_RP_PD] + 0.5f);
        pidkd[0] = pidkd_init[0] * (aux_analog[ANALOG_RP_PD] + 0.5f);
        pidkd[1] = pidkd_init[1] * (aux_analog[ANALOG_RP_PD] + 0.5f);
        analog_aux_pids_adjusted = 1;
    }
#endif
}




// pid calculation for acro ( rate ) mode
// input: error[x] = setpoint - gyro
// output: pidoutput[x] = change required from motors
float pid(int x )
{ 
    if ((aux[LEVELMODE]) && (!aux[RACEMODE])){
				if ((onground) || (in_air == 0)){
						ierror[x] *= 0.98f;}
		}else{
			  if (onground) ierror[x] *= 0.98f;
		}
	

// pid tuning via analog aux channels
#ifdef ANALOG_AUX_PIDS
    apply_analog_aux_to_pids();

#endif		
    
#ifdef TRANSIENT_WINDUP_PROTECTION
    static float avgSetpoint[3];
    static int count[3];
    extern float splpf( float in,int num );
    
    if ( x < 2 && (count[x]++ % 2) == 0 ) {
        avgSetpoint[x] = splpf( setpoint[x], x );
    }
#endif
    	
    int iwindup = 0;
    if (( pidoutput[x] == outlimit[x] )&& ( error[x] > 0) )
    {
        iwindup = 1;		
    }
    
    if (( pidoutput[x] == -outlimit[x])&& ( error[x] < 0) )
    {
        iwindup = 1;				
    } 
		
    
    #ifdef ANTI_WINDUP_DISABLE
    iwindup = 0;
    #endif
		

    #ifdef TRANSIENT_WINDUP_PROTECTION
	  if ( x < 2 && fabsf( setpoint[x] - avgSetpoint[x] ) > 0.1f ) {
		iwindup = 1;
	}
    #endif
    
    if ( !iwindup)
    {
        #ifdef MIDPOINT_RULE_INTEGRAL
         // trapezoidal rule instead of rectangular
        ierror[x] = ierror[x] + (error[x] + lasterror[x]) * 0.5f *  pidki[x] * looptime;
        lasterror[x] = error[x];
        #endif
            
        #ifdef RECTANGULAR_RULE_INTEGRAL
        ierror[x] = ierror[x] + error[x] *  pidki[x] * looptime;
        lasterror[x] = error[x];					
        #endif
            
        #ifdef SIMPSON_RULE_INTEGRAL
        // assuming similar time intervals
        ierror[x] = ierror[x] + 0.166666f* (lasterror2[x] + 4*lasterror[x] + error[x]) *  pidki[x] * looptime;	
        lasterror2[x] = lasterror[x];
        lasterror[x] = error[x];
        #endif					
    }
            
    limitf( &ierror[x] , integrallimit[x] );
    
    
    #ifdef ENABLE_SETPOINT_WEIGHTING
    // P term
    pidoutput[x] = error[x] * ( b[x])* pidkp[x];				
    // b
    pidoutput[x] +=  - ( 1.0f - b[x])* pidkp[x] * gyro[x];
    #else
    // P term with b disabled
    pidoutput[x] = error[x] * pidkp[x];
    #endif
    
    // I term	
    pidoutput[x] += ierror[x];

    // D term
    // skip yaw D term if not set               
    if ( pidkd[x] > 0 )
    {
        #ifdef NORMAL_DTERM
        pidoutput[x] = pidoutput[x] - (gyro[x] - lastrate[x]) * pidkd[x] * timefactor  ;
        lastrate[x] = gyro[x];
        #endif

        #ifdef NEW_DTERM
        pidoutput[x] = pidoutput[x] - ( ( 0.5f) *gyro[x] 
                    - (0.5f) * lastratexx[x][1] ) * pidkd[x] * timefactor  ;
                        
        lastratexx[x][1] = lastratexx[x][0];
        lastratexx[x][0] = gyro[x];
        #endif
    
        #ifdef MAX_FLAT_LPF_DIFF_DTERM 
        pidoutput[x] = pidoutput[x] - ( + 0.125f *gyro[x] + 0.250f * lastratexx[x][0]
                    - 0.250f * lastratexx[x][2] - ( 0.125f) * lastratexx[x][3]) * pidkd[x] * timefactor 						;

        lastratexx[x][3] = lastratexx[x][2];
        lastratexx[x][2] = lastratexx[x][1];
        lastratexx[x][1] = lastratexx[x][0];
        lastratexx[x][0] = gyro[x];
        #endif 


        #if (defined DTERM_LPF_1ST_HZ && !defined ADVANCED_PID_CONTROLLER)
        float dterm;
        static float lastrate[3];
        static float dlpf[3] = {0};

        dterm = - (gyro[x] - lastrate[x]) * pidkd[x] * timefactor;
        lastrate[x] = gyro[x];

        lpf( &dlpf[x], dterm, FILTERCALC( 0.001 , 1.0f/DTERM_LPF_1ST_HZ ) );

        pidoutput[x] += dlpf[x];                   
        #endif
				
        #if (defined DTERM_LPF_1ST_HZ && defined ADVANCED_PID_CONTROLLER)
				extern float rxcopy[4];		
        float dterm;		
				float transitionSetpointWeight[3];
				float stickAccelerator[3];
 			float stickTransition[3];
			if (aux[PIDPROFILE]){
				stickAccelerator[x] = stickAcceleratorProfileB[x];
				stickTransition[x] = stickTransitionProfileB[x];
			}else{
				stickAccelerator[x] = stickAcceleratorProfileA[x];
				stickTransition[x] = stickTransitionProfileA[x];
			}				
				if (stickAccelerator[x] < 1){
				transitionSetpointWeight[x] = (fabs(rxcopy[x]) * stickTransition[x]) + (1- stickTransition[x]);
			}else{
				transitionSetpointWeight[x] = (fabs(rxcopy[x]) * (stickTransition[x] / stickAccelerator[x])) + (1- stickTransition[x]);	
				}
        static float lastrate[3];
				static float lastsetpoint[3];
        static float dlpf[3] = {0};
        if ( pidkd[x] > 0){
						dterm = ((setpoint[x] - lastsetpoint[x]) * pidkd[x] * stickAccelerator[x] * transitionSetpointWeight[x] * timefactor) - ((gyro[x] - lastrate[x]) * pidkd[x] * timefactor);
						lastsetpoint[x] = setpoint [x];
						lastrate[x] = gyro[x];	
						lpf( &dlpf[x], dterm, FILTERCALC( 0.001 , 1.0f/DTERM_LPF_1ST_HZ ) );
						pidoutput[x] += dlpf[x]; }                   
        #endif	
     

        
        #if (defined DTERM_LPF_2ND_HZ && !defined ADVANCED_PID_CONTROLLER)
        float dterm;
        static float lastrate[3];			
        float lpf2( float in, int num);
        if ( pidkd[x] > 0)
        {
            dterm = - (gyro[x] - lastrate[x]) * pidkd[x] * timefactor;
            lastrate[x] = gyro[x];
            dterm = lpf2(  dterm, x );
            pidoutput[x] += dterm;
        }                       
        #endif
				
				#if (defined DTERM_LPF_2ND_HZ && defined ADVANCED_PID_CONTROLLER)
				extern float rxcopy[4];		
        float dterm;		
				float transitionSetpointWeight[3];
				float stickAccelerator[3];
				float stickTransition[3];
			if (aux[PIDPROFILE]){
				stickAccelerator[x] = stickAcceleratorProfileB[x];
				stickTransition[x] = stickTransitionProfileB[x];
			}else{
				stickAccelerator[x] = stickAcceleratorProfileA[x];
				stickTransition[x] = stickTransitionProfileA[x];
			}
				if (stickAccelerator[x] < 1){
				transitionSetpointWeight[x] = (fabs(rxcopy[x]) * stickTransition[x]) + (1- stickTransition[x]);
				}else{
				transitionSetpointWeight[x] = (fabs(rxcopy[x]) * (stickTransition[x] / stickAccelerator[x])) + (1- stickTransition[x]);	
				}
        static float lastrate[3];
				static float lastsetpoint[3];
        float lpf2( float in, int num);
        if ( pidkd[x] > 0){
						dterm = ((setpoint[x] - lastsetpoint[x]) * pidkd[x] * stickAccelerator[x] * transitionSetpointWeight[x] * timefactor) - ((gyro[x] - lastrate[x]) * pidkd[x] * timefactor);
						lastsetpoint[x] = setpoint [x];
						lastrate[x] = gyro[x];	
            dterm = lpf2(  dterm, x );
            pidoutput[x] += dterm;}
				#endif
	
    }
   

		
    limitf(  &pidoutput[x] , outlimit[x]);
		
		#ifdef PID_VOLTAGE_COMPENSATION
	pidoutput[x] *= v_compensation;
#endif

return pidoutput[x];		 		
}

// calculate change from ideal loop time
// 0.0032f is there for legacy purposes, should be 0.001f = looptime
// this is called in advance as an optimization because it has division
void pid_precalc()
{
	timefactor = 0.0032f / looptime;
	#ifdef PID_VOLTAGE_COMPENSATION
	v_compensation = mapf ( vbattfilt , 3.00 , 4.00 , PID_VC_FACTOR , 1.00);
	if( v_compensation > PID_VC_FACTOR) v_compensation = PID_VC_FACTOR;
	if( v_compensation < 1.00f) v_compensation = 1.00;
	#ifdef LEVELMODE_PID_ATTENUATION
	if (aux[LEVELMODE]) v_compensation *= LEVELMODE_PID_ATTENUATION;
	#endif
#endif

}

// call at quad startup, and when wanting to save pids
void pid_init()
{

  // save initial PID values
  pidkp_init[0] = pidkp[0]; // Roll
  pidkp_init[1] = pidkp[1]; // Pitch
  pidkp_init[2] = pidkp[2]; // Yaw

  pidki_init[0] = pidki[0];
  pidki_init[1] = pidki[1];
  pidki_init[2] = pidki[2];

  pidkd_init[0] = pidkd[0];
  pidkd_init[1] = pidkd[1];
  pidkd_init[2] = pidkd[2];

}


#ifndef DTERM_LPF_2ND_HZ 
#define DTERM_LPF_2ND_HZ 99
#endif

//the compiler calculates these
static float two_one_minus_alpha = 2*FILTERCALC( 0.001 , (1.0f/DTERM_LPF_2ND_HZ) );
static float one_minus_alpha_sqr = (FILTERCALC( 0.001 , (1.0f/DTERM_LPF_2ND_HZ) ) )*(FILTERCALC( 0.001 , (1.0f/DTERM_LPF_2ND_HZ) ));
static float alpha_sqr = (1 - FILTERCALC( 0.001 , (1.0f/DTERM_LPF_2ND_HZ) ))*(1 - FILTERCALC( 0.001 , (1.0f/DTERM_LPF_2ND_HZ) ));

static float last_out[3], last_out2[3];

float lpf2( float in, int num)
 {

  float ans = in * alpha_sqr + two_one_minus_alpha * last_out[num]
      - one_minus_alpha_sqr * last_out2[num];   

  last_out2[num] = last_out[num];
  last_out[num] = ans;
  
  return ans;
 }

// below are functions used with gestures for changing pids by a percentage

// Cycle through P / I / D - The initial value is P
// The return value is the currently selected TERM (after setting the next one)
// 1: P
// 2: I
// 3: D
// The return value is used to blink the leds in main.c
int next_pid_term()
{
//	current_pid_axis = 0;
	
	switch (current_pid_term)
	{
		case 0:
			current_pid_term_pointer = pidki;
		  current_pid_term_pointer1 = pidki1; // dual PIDs code
			current_pid_term_pointer2 = pidki2; // dual PIDs code
			current_pid_term = 1;
			break;
		case 1:
			current_pid_term_pointer = pidkd;
			current_pid_term_pointer1 = pidkd1; // dual PIDs code
			current_pid_term_pointer2 = pidkd2; // dual PIDs code
			current_pid_term = 2;
			break;
		case 2:
			current_pid_term_pointer = pidkp;
	    current_pid_term_pointer1 = pidkp1; // dual PIDs code
			current_pid_term_pointer2 = pidkp2; // dual PIDs code			
		  current_pid_term = 0;
			break;
	}
	
	return current_pid_term + 1;
}

// Cycle through the axis - Initial is Roll
// Return value is the selected axis, after setting the next one.
// 1: Roll
// 2: Pitch
// 3: Yaw
// The return value is used to blink the leds in main.c
int next_pid_axis()
{
	const int size = 3;
	if (current_pid_axis == size - 1) {
		current_pid_axis = 0;
	}
	else {
		#ifdef COMBINE_PITCH_ROLL_PID_TUNING
		if (current_pid_axis <2 ) {
			// Skip axis == 1 which is roll, and go directly to 2 (Yaw)
			current_pid_axis = 2;
		}
		#else
		current_pid_axis++;
		#endif
	}
	
	return current_pid_axis + 1;
}

#define PID_GESTURES_MULTI 1.1f

int change_pid_value(int increase)
{
	float multiplier = 1.0f/(float)PID_GESTURES_MULTI;
	if (increase) {
		multiplier = (float)PID_GESTURES_MULTI;
		number_of_increments[current_pid_term][current_pid_axis]++;
	}
	else {
		number_of_increments[current_pid_term][current_pid_axis]--;
	}
    
	current_pid_term_pointer[current_pid_axis] = current_pid_term_pointer[current_pid_axis] * multiplier;
	
// -------------- DUAL PIDS CODE -------------
#ifdef ENABLE_DUAL_PIDS
	extern char aux[AUXNUMBER];
	if (!aux[PID_SET_CHANGE])
	{
		current_pid_term_pointer1[current_pid_axis]=current_pid_term_pointer[current_pid_axis];
	} else
	{
		current_pid_term_pointer2[current_pid_axis]=current_pid_term_pointer[current_pid_axis];
	}	
#endif
#ifndef ENABLE_DUAL_PIDS
	current_pid_term_pointer1[current_pid_axis]=current_pid_term_pointer[current_pid_axis];
#endif
// -------------- END OF DUAL PIDS CODE -------------

    #ifdef COMBINE_PITCH_ROLL_PID_TUNING
	if (current_pid_axis == 0) {
		current_pid_term_pointer[current_pid_axis+1] = current_pid_term_pointer[current_pid_axis+1] * multiplier;

// -------------- DUAL PIDS CODE -------------
#ifdef ENABLE_DUAL_PIDS
	extern char aux[AUXNUMBER];
	if (!aux[PID_SET_CHANGE])
	{
		current_pid_term_pointer1[current_pid_axis+1]=current_pid_term_pointer[current_pid_axis+1];
	} else
	{
		current_pid_term_pointer2[current_pid_axis+1]=current_pid_term_pointer[current_pid_axis+1];
	}	
#endif
#ifndef ENABLE_DUAL_PIDS
		current_pid_term_pointer1[current_pid_axis+1]=current_pid_term_pointer[current_pid_axis+1];
#endif
// -------------- END OF DUAL PIDS CODE -------------
		
	}
	#endif
	
	return abs(number_of_increments[current_pid_term][current_pid_axis]);
}

// Increase currently selected term, for the currently selected axis, (by functions above) by 10%
// The return value, is absolute number of times the specific term/axis was increased or decreased.  For example, if P for Roll was increased by 10% twice,
// And then reduced by 10% 3 times, the return value would be 1  -  The user has to rememeber he has eventually reduced the by 10% and not increased by 10%
// I guess this can be improved by using the red leds for increments and blue leds for decrements or something, or just rely on SilverVISE
int increase_pid()
{
	return change_pid_value(1);
}

// Same as increase_pid but... you guessed it... decrease!
int decrease_pid()
{
	return change_pid_value(0);
}


void rotateErrors()
{

	// rotation around x axis:
	ierror[1] -= ierror[2] * gyro[0] * looptime;
	ierror[2] += ierror[1] * gyro[0] * looptime;

	// rotation around y axis:
	ierror[2] -= ierror[0] * gyro[1] * looptime;
	ierror[0] += ierror[2] * gyro[1] * looptime;

	// rotation around z axis:
	ierror[0] -= ierror[1] * gyro[2] * looptime;
	ierror[1] += ierror[0] * gyro[2] * looptime;

}






