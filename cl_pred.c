/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include "quakedef.h"
#include "pmove.h"

cvar_t	cl_nopred	= {"cl_nopred", "0"};
// shaman RFE 1036160 {
cvar_t	cl_pushlatency = {"pushlatency", "0"};
// } shaman RFE 1036160

extern cvar_t cl_independentPhysics;

void CL_PredictUsercmd (player_state_t *from, player_state_t *to, usercmd_t *u) {
	// split up very long moves
	if (u->msec > 50) {
		player_state_t temp;
		usercmd_t split;

		split = *u;
		split.msec /= 2;

		CL_PredictUsercmd (from, &temp, &split);
		CL_PredictUsercmd (&temp, to, &split);
		return;
	}

	VectorCopy (from->origin, pmove.origin);
	VectorCopy (u->angles, pmove.angles);
	VectorCopy (from->velocity, pmove.velocity);

	pmove.jump_msec = (cl.z_ext & Z_EXT_PM_TYPE) ? 0 : from->jump_msec;
	pmove.jump_held = from->jump_held;
	pmove.waterjumptime = from->waterjumptime;
	pmove.pm_type = from->pm_type;
	pmove.onground = from->onground;
	pmove.cmd = *u;

	movevars.entgravity = cl.entgravity;
	movevars.maxspeed = cl.maxspeed;
	movevars.bunnyspeedcap = cl.bunnyspeedcap;

	PM_PlayerMove ();

	to->waterjumptime = pmove.waterjumptime;
	to->pm_type = pmove.pm_type;
	to->jump_held = pmove.jump_held;
	to->jump_msec = pmove.jump_msec;
	pmove.jump_msec = 0;

	VectorCopy (pmove.origin, to->origin);
	VectorCopy (pmove.angles, to->viewangles);
	VectorCopy (pmove.velocity, to->velocity);
	to->onground = pmove.onground;

	to->weaponframe = from->weaponframe;
}

//Used when cl_nopred is 1 to determine whether we are on ground, otherwise stepup smoothing code produces ugly jump physics
void CL_CategorizePosition (void) {
	if (cl.spectator && cl.playernum == cl.viewplayernum) {
		cl.onground = false;	// in air
		return;
	}
	VectorClear (pmove.velocity);
	VectorCopy (cl.simorg, pmove.origin);
	pmove.numtouch = 0;
	PM_CategorizePosition ();
	cl.onground = pmove.onground;
}

//Smooth out stair step ups.
//Called before CL_EmitEntities so that the player's lightning model origin is updated properly
void CL_CalcCrouch (void) {
	qboolean teleported;
	static vec3_t oldorigin = {0, 0, 0};
	static float oldz = 0, extracrouch = 0, crouchspeed = 100;

	teleported = !VectorL2Compare(cl.simorg, oldorigin, 48);
	VectorCopy (cl.simorg, oldorigin);

	if (teleported) {
		// possibly teleported or respawned
		oldz = cl.simorg[2];
		extracrouch = 0;
		crouchspeed = 100;
		cl.crouch = 0;
		VectorCopy (cl.simorg, oldorigin);
		return;
	}

	if (cl.onground && cl.simorg[2] - oldz > 0) {
		if (cl.simorg[2] - oldz > 20) {
			// if on steep stairs, increase speed
			if (crouchspeed < 160) {
				extracrouch = cl.simorg[2] - oldz - cls.frametime * 200 - 15;
				extracrouch = min(extracrouch, 5);
			}
			crouchspeed = 160;
		}

		oldz += cls.frametime * crouchspeed;
		if (oldz > cl.simorg[2])
			oldz = cl.simorg[2];

		if (cl.simorg[2] - oldz > 15 + extracrouch)
			oldz = cl.simorg[2] - 15 - extracrouch;
		extracrouch -= cls.frametime * 200;
		extracrouch = max(extracrouch, 0);

		cl.crouch = oldz - cl.simorg[2];
	} else {
		// in air or moving down
		oldz = cl.simorg[2];
		cl.crouch += cls.frametime * 150;
		if (cl.crouch > 0)
			cl.crouch = 0;
		crouchspeed = 100;
		extracrouch = 0;
	}
}


extern qboolean physframe; //#fps

//#fps
// for fps-independent physics

static void CL_LerpMove (double msgtime, float f)
{
	
	static int		lastsequence = 0;
	static vec3_t	lerp_angles[3];
	static vec3_t	lerp_origin[3];
	static double	lerp_times[3];
	static qboolean	nolerp[2];
	static double	demo_latency = 0.01;
	float	frac;
	float	simtime;
	int		i;
	int		from, to;
	extern cvar_t cl_nolerp;

	if (cl_nolerp.value) {
lastsequence = ((unsigned)-1) >> 1;	//reset
		return;
	}

	if (cls.netchan.outgoing_sequence < lastsequence) {
		// reset
//Com_Printf ("*********** RESET");
		lastsequence = -1;
		lerp_times[0] = -1;
		demo_latency = 0.01;
	}

//@@	if (cls.netchan.outgoing_sequence > lastsequence) {
if (physframe) {	// #fps
		lastsequence = cls.netchan.outgoing_sequence;
		// move along
		lerp_times[2] = lerp_times[1];
		lerp_times[1] = lerp_times[0];
		lerp_times[0] = msgtime;

		VectorCopy (lerp_origin[1], lerp_origin[2]);
		VectorCopy (lerp_origin[0], lerp_origin[1]);
		VectorCopy (cl.simorg, lerp_origin[0]);

		VectorCopy (lerp_angles[1], lerp_angles[2]);
		VectorCopy (lerp_angles[0], lerp_angles[1]);
		VectorCopy (cl.simangles, lerp_angles[0]);

		nolerp[1] = nolerp[0];
		nolerp[0] = false;
		for (i = 0; i < 3; i++)
			if (fabs(lerp_origin[0][i] - lerp_origin[1][i]) > 100)
				break;
		if (i < 3)
			nolerp[0] = true;	// a teleport or something
	}

	simtime = cls.realtime - demo_latency;

	// adjust latency
	if (simtime > lerp_times[0]) {
//		Com_DPrintf ("HIGH clamp\n");
		demo_latency = cls.realtime - lerp_times[0];
	}
	else if (simtime < lerp_times[2]) {
//		Com_DPrintf ("   low clamp\n");
		demo_latency = cls.realtime - lerp_times[2];
	} else {
// extern cvar_t cl_physfps;
		// drift towards ideal latency
		float ideal_latency = (lerp_times[0] - lerp_times[2]) * 0.6;
//		float ideal_latency = 1.0/cl_physfps.value;

ideal_latency = 0;

if (physframe)	//##testing
{
		if (demo_latency > ideal_latency)
			demo_latency = max(demo_latency - cls.frametime * 0.1, ideal_latency);
		if (demo_latency < ideal_latency)
			demo_latency = min(demo_latency + cls.frametime * 0.1, ideal_latency);
}
	}

	// decide where to lerp from
	if (simtime > lerp_times[1]) {
		from = 1;
		to = 0;
	} else {
		from = 2;
		to = 1;
	}

	if (nolerp[to])
		return;

// shaman RFE 1036160 {
	if (cl_pushlatency.value != 0) {
        frac = f;
	}
	else {
// } shaman RFE 1036160 
    	frac = (simtime - lerp_times[from]) / (lerp_times[to] - lerp_times[from]);
    	frac = bound (0, frac, 1);
// shaman RFE 1036160 {
	}
// } shaman RFE 1036160 


//Com_Printf ("%f\n", frac);

	for (i = 0; i < 3; i++)
		cl.simorg[i] = lerp_origin[from][i] + (lerp_origin[to][i] - lerp_origin[from][i]) * frac;


}

double lerp_time;


void CL_PredictMove (void) {
	int i, oldphysent;
	frame_t *from = NULL, *to;
// shaman RFE 1036160 {
	double playertime;
    float f = 0;
	if (cl_pushlatency.value > 0)
		Cvar_Set (&cl_pushlatency, "0");

	playertime = cls.realtime - cls.latency - cl_pushlatency.value * 0.001;
	if (playertime > cls.realtime)
		playertime = cls.realtime;
// } shaman RFE 1036160 
	if (cl.paused)
		return;

	if (cl.intermission) {
		cl.crouch = 0;
		return;
	}

	if (!cl.validsequence)
		return;

	if (cls.netchan.outgoing_sequence - cl.validsequence >= UPDATE_BACKUP - 1)
		return;

	VectorCopy (cl.viewangles, cl.simangles);

	// this is the last valid frame received from the server
	to = &cl.frames[cl.validsequence & UPDATE_MASK];

	// FIXME...
	if (cls.demoplayback && cl.spectator && cl.viewplayernum != cl.playernum) {
		VectorCopy (to->playerstate[cl.viewplayernum].velocity, cl.simvel);
		VectorCopy (to->playerstate[cl.viewplayernum].origin, cl.simorg);
		VectorCopy (to->playerstate[cl.viewplayernum].viewangles, cl.simangles);
		CL_CategorizePosition ();
		goto out;
	}

	if ((cl_nopred.value && !cls.mvdplayback) || cl.validsequence + 1 >= cls.netchan.outgoing_sequence) {	
		VectorCopy (to->playerstate[cl.playernum].velocity, cl.simvel);
		VectorCopy (to->playerstate[cl.playernum].origin, cl.simorg);
		CL_CategorizePosition ();
		if (cl_independentPhysics.value != 0) 
			lerp_time = cls.realtime;	//#fps
		goto out;
	}

//#fps
if ((physframe && cl_independentPhysics.value != 0) || cl_independentPhysics.value == 0)
{
	oldphysent = pmove.numphysent;
	CL_SetSolidPlayers (cl.playernum);

	// run frames
	for (i = 1; i < UPDATE_BACKUP - 1 && cl.validsequence + i < cls.netchan.outgoing_sequence; i++) {
		from = to;
		to = &cl.frames[(cl.validsequence + i) & UPDATE_MASK];
		CL_PredictUsercmd (&from->playerstate[cl.playernum], &to->playerstate[cl.playernum], &to->cmd);
		cl.onground = pmove.onground;
		if (cl_pushlatency.value != 0 && to->senttime >= playertime)
			break; 
	}

	pmove.numphysent = oldphysent;

// shaman RFE 1036160 {
/*
	// copy results out for rendering
	VectorCopy (to->playerstate[cl.playernum].velocity, cl.simvel);
	VectorCopy (to->playerstate[cl.playernum].origin, cl.simorg);
	if (physframe && cl_independentPhysics.value != 0)
		lerp_time = cls.realtime;
*/

	if (cl_pushlatency.value != 0) {
		if (to->senttime == from->senttime) {
			f = 0;
		} else {
			f = (playertime - from->senttime) / (to->senttime - from->senttime);
			f = bound(0, f, 1);
		}
	}

//	for (i = 0; i < 3; i++) {
//		if ( fabs(from->playerstate[cl.playernum].origin[i] - to->playerstate[cl.playernum].origin[i]) > 128) {
			// teleported, so don't lerp
			VectorCopy (to->playerstate[cl.playernum].velocity, cl.simvel);
			VectorCopy (to->playerstate[cl.playernum].origin, cl.simorg);
//			goto out;
//		}
//	} 

	if (physframe && cl_independentPhysics.value != 0)
		lerp_time = playertime;

// } shaman RFE 1036160
}

out:
if (!cls.demoplayback && cl_independentPhysics.value != 0)
	CL_LerpMove (lerp_time, f);
    CL_CalcCrouch ();
	cl.waterlevel = pmove.waterlevel;
}

void CL_InitPrediction (void) {
	Cvar_SetCurrentGroup(CVAR_GROUP_NETWORK);
	Cvar_Register(&cl_nopred);
// shaman RFE 1036160 {
    Cvar_Register(&cl_pushlatency); 
// } shaman RFE 1036160

	Cvar_ResetCurrentGroup();
} 
