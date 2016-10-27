/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016      Spike

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
// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"
#include "bgmusic.h"

const char *svc_strings[] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",		// [long] server version
	"svc_setview",		// [short] entity number
	"svc_sound",			// <see code>
	"svc_time",			// [float] server time
	"svc_print",			// [string] null terminated string
	"svc_stufftext",		// [string] stuffed into client's console buffer
						// the string should be \n terminated
	"svc_setangle",		// [vec3] set the view angle to this absolute value

	"svc_serverinfo",		// [long] version
						// [string] signon string
						// [string]..[0]model cache [string]...[0]sounds cache
						// [string]..[0]item cache
	"svc_lightstyle",		// [byte] [string]
	"svc_updatename",		// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",		// <shortbits + data>
	"svc_stopsound",		// <see code>
	"svc_updatecolors",	// [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",			// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	/*"OBSOLETE svc_spawnbinary"*/"21 svc_spawnstatic_fte",
	"svc_spawnbaseline",

	"svc_temp_entity",		// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",			// [string] music [string] text
	"svc_cdtrack",			// [byte] track [byte] looptrack
	"svc_sellscreen",
	"svc_cutscene",
//johnfitz -- new server messages
	"35",	// 35
	"36",	// 36
	"svc_skybox", // 37					// [string] skyname
	"38", // 38
	"39", // 39
	"svc_bf", // 40						// no data
	"svc_fog", // 41					// [byte] density [byte] red [byte] green [byte] blue [float] time
	"svc_spawnbaseline2", //42			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstatic2", // 43			// support for large modelindex, large framenum, alpha, using flags
	"svc_spawnstaticsound2", //	44		// [coord3] [short] samp [byte] vol [byte] aten
	"45", // 45
	"46", // 46
	"47", // 47
	"48", // 48
	"49", // 49
//johnfitz

//spike -- particle stuff
	"50", // 50
	"51 svc_updatestatbyte", // 51
	"52", // 52
	"53", // 53
	"54 svc_precache", // 54	//[short] type+idx [string] name
	"55", // 55
	"56", // 56
	"57", // 57
	"58", // 58
	"59", // 59
	"60 svc_trailparticles", // 60
	"61 svc_pointparticles", // 61
	"62 svc_pointparticles1", // 62
	"63", // 63
	"64", // 64
	"65", // 65
	"66 svc_spawnbaseline_fte", // 66
	"67", // 67
	"68", // 68
	"69", // 69
	"70", // 70
	"71", // 71
	"72", // 72
	"73", // 73
	"74", // 74
	"75", // 75
	"76", // 76
	"77", // 77
	"78 svc_updatestatstring", // 78
	"79 svc_updatestatfloat", // 79
	"80", // 80
	"81", // 81
	"82", // 82
	"83", // 83
	"84", // 84
	"85", // 85
	"86 svc_updateentities_fte", // 86
//spike
};

qboolean warn_about_nehahra_protocol; //johnfitz

extern vec3_t	v_punchangles[2]; //johnfitz

//=============================================================================

/*
===============
CL_EntityNum

This error checks and tracks the total number of entities
===============
*/
entity_t	*CL_EntityNum (int num)
{
	//johnfitz -- check minimum number too
	if (num < 0)
		Host_Error ("CL_EntityNum: %i is an invalid number",num);
	//john

	if (num >= cl.num_entities)
	{
		if (num >= cl_max_edicts) //johnfitz -- no more MAX_EDICTS
			Host_Error ("CL_EntityNum: %i is an invalid number",num);
		while (cl.num_entities<=num)
		{
			cl_entities[cl.num_entities].colormap = vid.colormap;
			cl_entities[cl.num_entities].lerpflags |= LERP_RESETMOVE|LERP_RESETANIM; //johnfitz
			cl.num_entities++;
		}
	}

	return &cl_entities[num];
}





static int MSG_ReadEntity(void)
{
	int e = (unsigned short)MSG_ReadShort();
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (e & 0x8000)
		{
			e = (e & 0x7fff) << 8;
			e |= MSG_ReadByte();
		}
	}
	return e;
}
static entity_state_t nullentitystate;
static void CLFTE_SetupNullState(void)
{
	//the null state has some specific default values
//	nullentitystate.drawflags = /*SCALE_ORIGIN_ORIGIN*/96;
//	nullentitystate.colormod[0] = 32;
//	nullentitystate.colormod[1] = 32;
//	nullentitystate.colormod[2] = 32;
//	nullentitystate.glowmod[0] = 32;
//	nullentitystate.glowmod[1] = 32;
//	nullentitystate.glowmod[2] = 32;
	nullentitystate.alpha = 0;	//fte has 255 by default, with 0 for invisible. fitz uses 1 for invisible, 0 default, and 255=full alpha
//	nullentitystate.scale = 16;
//	nullentitystate.solidsize = 0;//ES_SOLID_BSP;
}

static unsigned int CLFTE_ReadDelta(unsigned int entnum, entity_state_t *news, const entity_state_t *olds, const entity_state_t *baseline)
{
	unsigned int predbits = 0;
	unsigned int bits;
	
	bits = MSG_ReadByte();
	if (bits & UF_EXTEND1)
		bits |= MSG_ReadByte()<<8;
	if (bits & UF_EXTEND2)
		bits |= MSG_ReadByte()<<16;
	if (bits & UF_EXTEND3)
		bits |= MSG_ReadByte()<<24;

	if (cl_shownet.value >= 3)
		Con_SafePrintf("%3i:     Update %4i 0x%x\n", msg_readcount, entnum, bits);

	if (bits & UF_RESET)
	{
//		Con_Printf("%3i: Reset %i @ %i\n", msg_readcount, entnum, cls.netchan.incoming_sequence);
		*news = *baseline;
	}
	else if (!olds)
	{
		/*reset got lost, probably the data will be filled in later - FIXME: we should probably ignore this entity*/
		Con_DPrintf("New entity %i without reset\n", entnum);
		*news = nullentitystate;
	}
	else
		*news = *olds;
	
	if (bits & UF_FRAME)
	{
		if (bits & UF_16BIT)
			news->frame = MSG_ReadShort();
		else
			news->frame = MSG_ReadByte();
	}

	if (bits & UF_ORIGINXY)
	{
		news->origin[0] = MSG_ReadCoord(cl.protocolflags);
		news->origin[1] = MSG_ReadCoord(cl.protocolflags);
	}
	if (bits & UF_ORIGINZ)
		news->origin[2] = MSG_ReadCoord(cl.protocolflags);

	if ((bits & UF_PREDINFO) && !(cl.protocol_pext2 & PEXT2_PREDINFO))
	{
		//predicted stuff gets more precise angles
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle16(cl.protocolflags);
			news->angles[2] = MSG_ReadAngle16(cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle16(cl.protocolflags);
	}
	else
	{
		if (bits & UF_ANGLESXZ)
		{
			news->angles[0] = MSG_ReadAngle(cl.protocolflags);
			news->angles[2] = MSG_ReadAngle(cl.protocolflags);
		}
		if (bits & UF_ANGLESY)
			news->angles[1] = MSG_ReadAngle(cl.protocolflags);
	}

	if ((bits & (UF_EFFECTS | UF_EFFECTS2)) == (UF_EFFECTS | UF_EFFECTS2))
		news->effects = MSG_ReadLong();
	else if (bits & UF_EFFECTS2)
		news->effects = (unsigned short)MSG_ReadShort();
	else if (bits & UF_EFFECTS)
		news->effects = MSG_ReadByte();

//	news->movement[0] = 0;
//	news->movement[1] = 0;
//	news->movement[2] = 0;
	news->velocity[0] = 0;
	news->velocity[1] = 0;
	news->velocity[2] = 0;
	if (bits & UF_PREDINFO)
	{
		predbits = MSG_ReadByte();

		if (predbits & UFP_FORWARD)
			/*news->movement[0] =*/ MSG_ReadShort();
		//else
		//	news->movement[0] = 0;
		if (predbits & UFP_SIDE)
			/*news->movement[1] =*/ MSG_ReadShort();
		//else
		//	news->movement[1] = 0;
		if (predbits & UFP_UP)
			/*news->movement[2] =*/ MSG_ReadShort();
		//else
		//	news->movement[2] = 0;
		if (predbits & UFP_MOVETYPE)
			news->pmovetype = MSG_ReadByte();
		if (predbits & UFP_VELOCITYXY)
		{
			news->velocity[0] = MSG_ReadShort();
			news->velocity[1] = MSG_ReadShort();
		}
		else
		{
			news->velocity[0] = 0;
			news->velocity[1] = 0;
		}
		if (predbits & UFP_VELOCITYZ)
			news->velocity[2] = MSG_ReadShort();
		else
			news->velocity[2] = 0;
		if (predbits & UFP_MSEC)	//the msec value is how old the update is (qw clients normally predict without the server running an update every frame)
			/*news->msec =*/ MSG_ReadByte();
		//else
		//	news->msec = 0;

		if (cl.protocol_pext2 & PEXT2_PREDINFO)
		{
			if (predbits & UFP_VIEWANGLE)
			{
				if (bits & UF_ANGLESXZ)
				{
					/*news->vangle[0] =*/ MSG_ReadShort();
					/*news->vangle[2] =*/ MSG_ReadShort();
				}
				if (bits & UF_ANGLESY)
					/*news->vangle[1] =*/ MSG_ReadShort();
			}
		}
		else
		{
			if (predbits & UFP_WEAPONFRAME_OLD)
			{
				int wframe;
				wframe = MSG_ReadByte();
				if (wframe & 0x80)
					wframe = (wframe & 127) | (MSG_ReadByte()<<7);
			}
		}
	}
	else
	{
		//news->msec = 0;
	}

	if (!(predbits & UFP_VIEWANGLE) || !(cl.protocol_pext2 & PEXT2_PREDINFO))
	{/*
		if (bits & UF_ANGLESXZ)
			news->vangle[0] = ANGLE2SHORT(news->angles[0] * ((bits & UF_PREDINFO)?-3:-1));
		if (bits & UF_ANGLESY)
			news->vangle[1] = ANGLE2SHORT(news->angles[1]);
		if (bits & UF_ANGLESXZ)
			news->vangle[2] = ANGLE2SHORT(news->angles[2]);
		*/
	}

	if (bits & UF_MODEL)
	{
		if (bits & UF_16BIT)
			news->modelindex = MSG_ReadShort();
		else
			news->modelindex = MSG_ReadByte();
	}
	if (bits & UF_SKIN)
	{
		if (bits & UF_16BIT)
			news->skin = MSG_ReadShort();
		else
			news->skin = MSG_ReadByte();
	}
	if (bits & UF_COLORMAP)
		news->colormap = MSG_ReadByte();

	if (bits & UF_SOLID)
	{	//knowing the size of an entity is important for prediction
		/*if (cl.protocol_pext2 & PEXT2_NEWSIZEENCODING)
		{
			qbyte enc = MSG_ReadByte();
			if (enc == 0)
				solidsize = ES_SOLID_NOT;
			else if (enc == 1)
				solidsize = ES_SOLID_BSP;
			else if (enc == 2)
				solidsize = ES_SOLID_HULL1;
			else if (enc == 3)
				solidsize = ES_SOLID_HULL2;
			else if (enc == 16)
				solidsize = MSG_ReadSize16(&net_message);
			else if (enc == 32)
				solidsize = MSG_ReadLong();
			else
				Sys_Error("Solid+Size encoding not known");
		}
		else
			solidsize =*/ MSG_ReadShort();//MSG_ReadSize16(&net_message);
//		news->solidsize = solidsize;
	}

	if (bits & UF_FLAGS)
		news->eflags = MSG_ReadByte();

	if (bits & UF_ALPHA)
		news->alpha = (MSG_ReadByte()+1)&0xff;
	if (bits & UF_SCALE)
		/*news->scale =*/ MSG_ReadByte();
	if (bits & UF_BONEDATA)
	{
		unsigned char fl = MSG_ReadByte();
		if (fl & 0x80)
		{
			//this is NOT finalized
			int i;
			int bonecount = MSG_ReadByte();
			//short *bonedata = AllocateBoneSpace(newp, bonecount, &news->boneoffset);
			for (i = 0; i < bonecount*7; i++)
				/*bonedata[i] =*/ MSG_ReadShort();
			//news->bonecount = bonecount;
		}
		//else
			//news->bonecount = 0;	//oo, it went away.
		if (fl & 0x40)
		{
			/*news->basebone =*/ MSG_ReadByte();
			/*news->baseframe =*/ MSG_ReadShort();
		}
		/*else
		{
			news->basebone = 0;
			news->baseframe = 0;
		}*/

		//fixme: basebone, baseframe, etc.
		if (fl & 0x3f)
			Host_EndGame("unsupported entity delta info\n");
	}
//	else if (news->bonecount)
//	{	//still has bone data from the previous frame.
//		short *bonedata = AllocateBoneSpace(newp, news->bonecount, &news->boneoffset);
//		memcpy(bonedata, oldp->bonedata+olds->boneoffset, sizeof(short)*7*news->bonecount);
//	}

	if (bits & UF_DRAWFLAGS)
	{
		int drawflags = MSG_ReadByte();
		if ((drawflags & /*MLS_MASK*/7) == /*MLS_ABSLIGHT*/7)
			/*news->abslight =*/ MSG_ReadByte();
		//else
		//	news->abslight = 0;
		//news->drawflags = drawflags;
	}
	if (bits & UF_TAGINFO)
	{
		/*news->tagentity =*/ MSG_ReadEntity();
		/*news->tagindex =*/ MSG_ReadByte();
	}
	if (bits & UF_LIGHT)
	{
		/*news->light[0] =*/ MSG_ReadShort();
		/*news->light[1] =*/ MSG_ReadShort();
		/*news->light[2] =*/ MSG_ReadShort();
		/*news->light[3] =*/ MSG_ReadShort();
		/*news->lightstyle =*/ MSG_ReadByte();
		/*news->lightpflags =*/ MSG_ReadByte();
	}
	if (bits & UF_TRAILEFFECT)
		news->traileffectnum = MSG_ReadShort();

	if (bits & UF_COLORMOD)
	{
		/*news->colormod[0] =*/ MSG_ReadByte();
		/*news->colormod[1] =*/ MSG_ReadByte();
		/*news->colormod[2] =*/ MSG_ReadByte();
	}
	if (bits & UF_GLOW)
	{
		/*news->glowsize =*/ MSG_ReadByte();
		/*news->glowcolour =*/ MSG_ReadByte();
		/*news->glowmod[0] =*/ MSG_ReadByte();
		/*news->glowmod[1] =*/ MSG_ReadByte();
		/*news->glowmod[2] =*/ MSG_ReadByte();
	}
	if (bits & UF_FATNESS)
		/*news->fatness =*/ MSG_ReadByte();
	if (bits & UF_MODELINDEX2)
	{
		if (bits & UF_16BIT)
			/*news->modelindex2 =*/ MSG_ReadShort();
		else
			/*news->modelindex2 =*/ MSG_ReadByte();
	}
	if (bits & UF_GRAVITYDIR)
	{
		/*news->gravitydir[0] =*/ MSG_ReadByte();
		/*news->gravitydir[1] =*/ MSG_ReadByte();
	}
	if (bits & UF_UNUSED2)
	{
		Host_EndGame("UF_UNUSED2 bit\n");
	}
	if (bits & UF_UNUSED1)
	{
		Host_EndGame("UF_UNUSED1 bit\n");
	}
	return bits;
}
static void CLFTE_ParseBaseline(entity_state_t *es)
{
	CLFTE_ReadDelta(0, es, &nullentitystate, &nullentitystate);
}

static void CLFTE_ParseEntitiesUpdate(void)
{
	int newnum;
	qboolean removeflag;
	entity_t *ent;
	float newtime;

	//so the server can know when we got it, and guess which frames we didn't get
	if (cl.ackframes_count < sizeof(cl.ackframes)/sizeof(cl.ackframes[0]))
		cl.ackframes[cl.ackframes_count++] = NET_QSocketGetSequenceIn(cls.netcon);

	if (cl.protocol_pext2 & PEXT2_PREDINFO)
		MSG_ReadShort();	//an ack from our input sequences. strictly ascending-or-equal

	newtime = MSG_ReadFloat ();
	if (newtime != cl.mtime[0])
	{	//don't mess up lerps if the server is splitting entities into multiple packets.
		cl.mtime[1] = cl.mtime[0];
		cl.mtime[0] = newtime;
	}

	for (;;)
	{
		newnum = (unsigned short)(short)MSG_ReadShort();
		removeflag = !!(newnum & 0x8000);
		if (newnum & 0x4000)
			newnum = (newnum & 0x3fff) | (MSG_ReadByte()<<14);
		else
			newnum &= ~0x8000;

		if ((!newnum && !removeflag) || msg_badread)
			break;

		ent = CL_EntityNum(newnum);

		if (removeflag)
		{
			if (cl_shownet.value >= 3)
				Con_SafePrintf("%3i:     Remove %i\n", msg_readcount, newnum);

			if (!newnum)
			{
				/*removal of world - means forget all entities, aka a full reset*/
				if (cl_shownet.value >= 3)
					Con_SafePrintf("%3i:     Reset all\n", msg_readcount);
				for (newnum = 1; newnum < cl.num_entities; newnum++)
				{
					CL_EntityNum(newnum)->netstate.pmovetype = 0;
					CL_EntityNum(newnum)->model = NULL;
				}
				cl.requestresend = false;	//we got it.
				continue;
			}
			ent->update_type = false; //no longer valid
			ent->model = NULL;
			continue;
		}
		else
		{
			CLFTE_ReadDelta(newnum, &ent->netstate, ent->update_type?&ent->netstate:NULL, &ent->baseline);
			ent->update_type = true;
		}
	}


	for (newnum = 1; newnum < cl.num_entities; newnum++)
	{
		int			i;
		qmodel_t	*model;
		qboolean	forcelink;
		entity_t	*ent;
		int			skin;

		ent = CL_EntityNum(newnum);
		if (!ent->update_type)
			continue;	//not interested in this one

		if (ent->msgtime != cl.mtime[1])
			forcelink = true;	// no previous frame to lerp from
		else
			forcelink = false;

		//johnfitz -- lerping
		if (ent->msgtime + 0.2 < cl.mtime[0]) //more than 0.2 seconds since the last message (most entities think every 0.1 sec)
			ent->lerpflags |= LERP_RESETANIM; //if we missed a think, we'd be lerping from the wrong frame

		ent->msgtime = cl.mtime[0];

		ent->frame = ent->netstate.frame;

		i = ent->netstate.colormap;
		if (!i)
			ent->colormap = vid.colormap;
		else
		{
			if (i > cl.maxclients)
				Sys_Error ("i >= cl.maxclients");
			ent->colormap = cl.scores[i-1].translations;
		}
		skin = ent->netstate.skin;
		if (skin != ent->skinnum)
		{
			ent->skinnum = skin;
			if (newnum > 0 && newnum <= cl.maxclients)
				R_TranslateNewPlayerSkin (newnum - 1); //johnfitz -- was R_TranslatePlayerSkin
		}
		ent->effects = ent->netstate.effects;

	// shift the known values for interpolation
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

		ent->msg_origins[0][0] = ent->netstate.origin[0];
		ent->msg_angles[0][0] = ent->netstate.angles[0];
		ent->msg_origins[0][1] = ent->netstate.origin[1];
		ent->msg_angles[0][1] = ent->netstate.angles[1];
		ent->msg_origins[0][2] = ent->netstate.origin[2];
		ent->msg_angles[0][2] = ent->netstate.angles[2];

		//johnfitz -- lerping for movetype_step entities
		if (ent->netstate.eflags & EFLAGS_STEP)
		{
			ent->lerpflags |= LERP_MOVESTEP;
			ent->forcelink = true;
		}
		else
			ent->lerpflags &= ~LERP_MOVESTEP;

		ent->alpha = ent->netstate.alpha;
		if (ent->alpha == 255)
			ent->alpha = 0;	//allows it to use r_wateralpha etc if its a water brush.
/*		if (bits & U_LERPFINISH)
		{
			ent->lerpfinish = ent->msgtime + ((float)(MSG_ReadByte()) / 255);
			ent->lerpflags |= LERP_FINISH;
		}
		else*/
			ent->lerpflags &= ~LERP_FINISH;

		model = cl.model_precache[ent->netstate.modelindex];
		if (model != ent->model)
		{
			ent->model = model;
		// automatic animation (torches, etc) can be either all together
		// or randomized
			if (model)
			{
				if (model->synctype == ST_RAND)
					ent->syncbase = (float)(rand()&0x7fff) / 0x7fff;
				else
					ent->syncbase = 0.0;
			}
			else
				forcelink = true;	// hack to make null model players work
			if (newnum > 0 && newnum <= cl.maxclients)
				R_TranslateNewPlayerSkin (newnum - 1); //johnfitz -- was R_TranslatePlayerSkin

			ent->lerpflags |= LERP_RESETANIM; //johnfitz -- don't lerp animation across model changes
		}

		if ( forcelink )
		{	// didn't have an update last message
			VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
			VectorCopy (ent->msg_angles[0], ent->angles);
			ent->forcelink = true;
		}
	}

	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{
		VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
		ent = CL_EntityNum(cl.viewentity);
		cl.mvelocity[0][0] = ent->netstate.velocity[0]*(1/8.0);
		cl.mvelocity[0][1] = ent->netstate.velocity[1]*(1/8.0);
		cl.mvelocity[0][2] = ent->netstate.velocity[2]*(1/8.0);
		cl.onground = (ent->netstate.eflags & EFLAGS_ONGROUND)?true:false;
	}

	if (!cl.requestresend)
	{
		if (cls.signon == SIGNONS - 1)
		{	// first update is the final signon stage
			cls.signon = SIGNONS;
			CL_SignonReply ();
		}
	}
}



/*
==================
CL_ParseStartSoundPacket
==================
*/
static void CL_ParseStartSoundPacket(void)
{
	vec3_t	pos;
	int	channel, ent;
	int	sound_num;
	int	volume;
	int	field_mask;
	float	attenuation;
	int	i;

	field_mask = MSG_ReadByte();

	if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
	{
		if (field_mask & (SND_FTE_MOREFLAGS|SND_FTE_PITCHADJ|SND_FTE_TIMEOFS))
		{
			field_mask &= ~(SND_FTE_MOREFLAGS|SND_FTE_PITCHADJ|SND_FTE_TIMEOFS);
			Con_Warning("Unknown meaning for sound flags\n");
		}
	}

	//spike -- extra channel flags
	if (field_mask & SND_FTE_MOREFLAGS)
		field_mask |= MSG_ReadByte()<<8;

	if (field_mask & SND_VOLUME)
		volume = MSG_ReadByte ();
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;

	if (field_mask & SND_ATTENUATION)
		attenuation = MSG_ReadByte () / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	//spike -- our mixer can't deal with these, so just parse and ignore
	if (field_mask & SND_FTE_PITCHADJ)
		MSG_ReadByte();	//percentage
	if (field_mask & SND_FTE_TIMEOFS)
		MSG_ReadShort(); //in ms
	if (field_mask & SND_FTE_VELOCITY)
	{
		MSG_ReadShort(); //1/8th
		MSG_ReadShort(); //1/8th
		MSG_ReadShort(); //1/8th
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (field_mask & SND_LARGEENTITY)
	{
		ent = (unsigned short) MSG_ReadShort ();
		channel = MSG_ReadByte ();
	}
	else
	{
		channel = (unsigned short) MSG_ReadShort ();
		ent = channel >> 3;
		channel &= 7;
	}

	if (field_mask & SND_LARGESOUND)
		sound_num = (unsigned short) MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	//johnfitz

	//johnfitz -- check soundnum
	if (sound_num >= MAX_SOUNDS)
		Host_Error ("CL_ParseStartSoundPacket: %i > MAX_SOUNDS", sound_num);
	//johnfitz

	if (ent > cl_max_edicts) //johnfitz -- no more MAX_EDICTS
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);

	for (i = 0; i < 3; i++)
		pos[i] = MSG_ReadCoord (cl.protocolflags);

	S_StartSound (ent, channel, cl.sound_precache[sound_num], pos, volume/255.0, attenuation);
}

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
static byte	net_olddata[NET_MAXMESSAGE];
static void CL_KeepaliveMessage (void)
{
	float	time;
	static float lastmsg;
	int		ret;
	sizebuf_t	old;
	byte	*olddata;

	if (sv.active)
		return;		// no need if server is local
	if (cls.demoplayback)
		return;

// read messages from server, should just be nops
	olddata = net_olddata;
	old = net_message;
	memcpy (olddata, net_message.data, net_message.cursize);

	do
	{
		ret = CL_GetMessage ();
		switch (ret)
		{
		default:
			Host_Error ("CL_KeepaliveMessage: CL_GetMessage failed");
		case 0:
			break;	// nothing waiting
		case 1:
			Host_Error ("CL_KeepaliveMessage: received a message");
			break;
		case 2:
			if (MSG_ReadByte() != svc_nop)
				Host_Error ("CL_KeepaliveMessage: datagram wasn't a nop");
			break;
		}
	} while (ret);

	net_message = old;
	memcpy (net_message.data, olddata, net_message.cursize);

// check time
	time = Sys_DoubleTime ();
	if (time - lastmsg < 5)
		return;
	lastmsg = time;

// write out a nop
	Con_Printf ("--> client to server keepalive\n");

	MSG_WriteByte (&cls.message, clc_nop);
	NET_SendMessage (cls.netcon, &cls.message);
	SZ_Clear (&cls.message);
}

/*
==================
CL_ParseServerInfo
==================
*/
static void CL_ParseServerInfo (void)
{
	const char	*str;
	int		i;
	int		nummodels, numsounds;
	char	model_precache[MAX_MODELS][MAX_QPATH];
	char	sound_precache[MAX_SOUNDS][MAX_QPATH];
	char	gamedir[1024];

	Con_DPrintf ("Serverinfo packet received.\n");

// ericw -- bring up loading plaque for map changes within a demo.
//          it will be hidden in CL_SignonReply.
	if (cls.demoplayback)
		SCR_BeginLoadingPlaque();

//
// wipe the client_state_t struct
//
	CL_ClearState ();

// parse protocol version number
	for(;;)
	{
		i = MSG_ReadLong ();
		if (i == PROTOCOL_FTE_PEXT2)
		{
			cl.protocol_pext2 = MSG_ReadLong();
			if (cl.protocol_pext2 & ~PEXT2_SUPPORTED_CLIENT)
				Host_Error ("Server returned FTE protocol extensions that are not supported (%#x)", cl.protocol_pext2 & ~PEXT2_SUPPORTED_CLIENT);
			continue;
		}
		break;
	}

	//johnfitz -- support multiple protocols
	if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ) {
		Con_Printf ("\n"); //because there's no newline after serverinfo print
		Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
	}
	cl.protocol = i;
	//johnfitz

	if (cl.protocol == PROTOCOL_RMQ)
	{
		const unsigned int supportedflags = (PRFL_SHORTANGLE | PRFL_FLOATANGLE | PRFL_24BITCOORD | PRFL_FLOATCOORD | PRFL_EDICTSCALE | PRFL_INT32COORD);
		
		// mh - read protocol flags from server so that we know what protocol features to expect
		cl.protocolflags = (unsigned int) MSG_ReadLong ();
		
		if (0 != (cl.protocolflags & (~supportedflags)))
		{
			Con_Warning("PROTOCOL_RMQ protocolflags %i contains unsupported flags\n", cl.protocolflags);
		}
	}
	else cl.protocolflags = 0;

	*gamedir = 0;
	if (cl.protocol_pext2 & PEXT2_PREDINFO)
	{
		q_strlcpy(gamedir, MSG_ReadString(), sizeof(gamedir));
		if (*gamedir)
		{
			const char *servergamedir = gamedir;
			//FIXME: automatically switch
			while(strchr(servergamedir, ';'))	//ignore multiples, just show the last. because we're lame
				servergamedir = strchr(servergamedir, ';')+1;
			if (!*servergamedir || !strcmp(servergamedir, "qw"))
				servergamedir = "id1";
			if (!q_strcasecmp(servergamedir, COM_SkipPath(com_gamedir)))
				*gamedir = 0;
		}
	}
	
// parse maxclients
	cl.maxclients = MSG_ReadByte ();
	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD)
	{
		Host_Error ("Bad maxclients (%u) from server", cl.maxclients);
	}
	cl.scores = (scoreboard_t *) Hunk_AllocName (cl.maxclients*sizeof(*cl.scores), "scores");

// parse gametype
	cl.gametype = MSG_ReadByte ();

// parse signon message
	str = MSG_ReadString ();
	q_strlcpy (cl.levelname, str, sizeof(cl.levelname));

// seperate the printfs so the server message can have a color
	Con_Printf ("\n%s\n", Con_Quakebar(40)); //johnfitz
	Con_Printf ("%c%s\n", 2, str);

//johnfitz -- tell user which protocol this is
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		Con_Printf ("Using protocol FTE+%i", i);
	else
		Con_Printf ("Using protocol %i", i);
	if (i == PROTOCOL_NETQUAKE && NET_QSocketGetProQuakeAngleHack(cls.netcon))
		Con_Printf ("+ang\n");	
	Con_Printf ("\n");

// first we go through and touch all of the precache data that still
// happens to be in the cache, so precaching something else doesn't
// needlessly purge it

// precache models
	memset (cl.model_precache, 0, sizeof(cl.model_precache));
	for (nummodels = 1 ; ; nummodels++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (nummodels==MAX_MODELS)
		{
			Host_Error ("Server sent too many model precaches");
		}
		q_strlcpy (model_precache[nummodels], str, MAX_QPATH);
		Mod_TouchModel (str);
	}

	//johnfitz -- check for excessive models
	if (nummodels >= 256)
		Con_DWarning ("%i models exceeds standard limit of 256.\n", nummodels);
	//johnfitz

// precache sounds
	memset (cl.sound_precache, 0, sizeof(cl.sound_precache));
	for (numsounds = 1 ; ; numsounds++)
	{
		str = MSG_ReadString ();
		if (!str[0])
			break;
		if (numsounds==MAX_SOUNDS)
		{
			Host_Error ("Server sent too many sound precaches");
		}
		q_strlcpy (sound_precache[numsounds], str, MAX_QPATH);
		S_TouchSound (str);
	}

	//johnfitz -- check for excessive sounds
	if (numsounds >= 256)
		Con_DWarning ("%i sounds exceeds standard limit of 256.\n", numsounds);
	//johnfitz

//
// now we try to load everything else until a cache allocation fails
//

	// copy the naked name of the map file to the cl structure -- O.S
	COM_StripExtension (COM_SkipPath(model_precache[1]), cl.mapname, sizeof(cl.mapname));

	for (i = 1; i < nummodels; i++)
	{
		cl.model_precache[i] = Mod_ForName (model_precache[i], false);
		if (cl.model_precache[i] == NULL)
		{
			Host_Error ("Model %s not found", model_precache[i]);
		}
		CL_KeepaliveMessage ();
	}

	S_BeginPrecaching ();
	for (i = 1; i < numsounds; i++)
	{
		cl.sound_precache[i] = S_PrecacheSound (sound_precache[i]);
		CL_KeepaliveMessage ();
	}
	S_EndPrecaching ();

// local state
	cl_entities[0].model = cl.worldmodel = cl.model_precache[1];

	R_NewMap ();

	//johnfitz -- clear out string; we don't consider identical
	//messages to be duplicates if the map has changed in between
	con_lastcenterstring[0] = 0;
	//johnfitz

	Hunk_Check ();		// make sure nothing is hurt

	noclip_anglehack = false;		// noclip is turned off at start

	warn_about_nehahra_protocol = true; //johnfitz -- warn about nehahra protocol hack once per server connection

//johnfitz -- reset developer stats
	memset(&dev_stats, 0, sizeof(dev_stats));
	memset(&dev_peakstats, 0, sizeof(dev_peakstats));
	memset(&dev_overflows, 0, sizeof(dev_overflows));

	CLFTE_SetupNullState();
	cl.requestresend = true;
	cl.ackframes_count = 0;
	if (cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS)
		cl.ackframes[cl.ackframes_count++] = -1;

	//this is here, to try to make sure its a little more obvious that its there.
	if (*gamedir)
	{
		Con_Warning("Server is using a different gamedir.\n");
		Con_Warning("Current: %s\n", COM_SkipPath(com_gamedir));
		Con_Warning("Server: %s\n", gamedir);
		Con_Warning("You will probably want to switch gamedir to match the server.\n");
	}
}

/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
static void CL_ParseUpdate (int bits)
{
	int		i;
	qmodel_t	*model;
	int		modnum;
	qboolean	forcelink;
	entity_t	*ent;
	int		num;
	int		skin;

	if (cls.signon == SIGNONS - 1)
	{	// first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply ();
	}

	if (bits & U_MOREBITS)
	{
		i = MSG_ReadByte ();
		bits |= (i<<8);
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_EXTEND1)
			bits |= MSG_ReadByte() << 16;
		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte() << 24;
	}
	//johnfitz

	if (bits & U_LONGENTITY)
		num = MSG_ReadShort ();
	else
		num = MSG_ReadByte ();

	ent = CL_EntityNum (num);

	if (ent->msgtime != cl.mtime[1])
		forcelink = true;	// no previous frame to lerp from
	else
		forcelink = false;

	//johnfitz -- lerping
	if (ent->msgtime + 0.2 < cl.mtime[0]) //more than 0.2 seconds since the last message (most entities think every 0.1 sec)
		ent->lerpflags |= LERP_RESETANIM; //if we missed a think, we'd be lerping from the wrong frame
	//johnfitz

	ent->msgtime = cl.mtime[0];

	if (bits & U_MODEL)
	{
		modnum = MSG_ReadByte ();
		if (modnum >= MAX_MODELS)
			Host_Error ("CL_ParseModel: bad modnum");
	}
	else
		modnum = ent->baseline.modelindex;

	if (bits & U_FRAME)
		ent->frame = MSG_ReadByte ();
	else
		ent->frame = ent->baseline.frame;

	if (bits & U_COLORMAP)
		i = MSG_ReadByte();
	else
		i = ent->baseline.colormap;
	if (!i)
		ent->colormap = vid.colormap;
	else
	{
		if (i > cl.maxclients)
			Sys_Error ("i >= cl.maxclients");
		ent->colormap = cl.scores[i-1].translations;
	}
	if (bits & U_SKIN)
		skin = MSG_ReadByte();
	else
		skin = ent->baseline.skin;
	if (skin != ent->skinnum)
	{
		ent->skinnum = skin;
		if (num > 0 && num <= cl.maxclients)
			R_TranslateNewPlayerSkin (num - 1); //johnfitz -- was R_TranslatePlayerSkin
	}
	if (bits & U_EFFECTS)
		ent->effects = MSG_ReadByte();
	else
		ent->effects = ent->baseline.effects;

// shift the known values for interpolation
	VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
	VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);

	if (bits & U_ORIGIN1)
		ent->msg_origins[0][0] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][0] = ent->baseline.origin[0];
	if (bits & U_ANGLE1)
		ent->msg_angles[0][0] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][0] = ent->baseline.angles[0];

	if (bits & U_ORIGIN2)
		ent->msg_origins[0][1] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][1] = ent->baseline.origin[1];
	if (bits & U_ANGLE2)
		ent->msg_angles[0][1] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][1] = ent->baseline.angles[1];

	if (bits & U_ORIGIN3)
		ent->msg_origins[0][2] = MSG_ReadCoord (cl.protocolflags);
	else
		ent->msg_origins[0][2] = ent->baseline.origin[2];
	if (bits & U_ANGLE3)
		ent->msg_angles[0][2] = MSG_ReadAngle(cl.protocolflags);
	else
		ent->msg_angles[0][2] = ent->baseline.angles[2];

	//johnfitz -- lerping for movetype_step entities
	if (bits & U_STEP)
	{
		ent->lerpflags |= LERP_MOVESTEP;
		ent->forcelink = true;
	}
	else
		ent->lerpflags &= ~LERP_MOVESTEP;
	//johnfitz

	//johnfitz -- PROTOCOL_FITZQUAKE and PROTOCOL_NEHAHRA
	if (cl.protocol == PROTOCOL_FITZQUAKE || cl.protocol == PROTOCOL_RMQ)
	{
		if (bits & U_ALPHA)
			ent->alpha = MSG_ReadByte();
		else
			ent->alpha = ent->baseline.alpha;
		if (bits & U_SCALE)
			MSG_ReadByte(); // PROTOCOL_RMQ: currently ignored
		if (bits & U_FRAME2)
			ent->frame = (ent->frame & 0x00FF) | (MSG_ReadByte() << 8);
		if (bits & U_MODEL2)
			modnum = (modnum & 0x00FF) | (MSG_ReadByte() << 8);
		if (bits & U_LERPFINISH)
		{
			ent->lerpfinish = ent->msgtime + ((float)(MSG_ReadByte()) / 255);
			ent->lerpflags |= LERP_FINISH;
		}
		else
			ent->lerpflags &= ~LERP_FINISH;
	}
	else if (cl.protocol == PROTOCOL_NETQUAKE)
	{
		//HACK: if this bit is set, assume this is PROTOCOL_NEHAHRA
		if (bits & U_TRANS)
		{
			float a, b;

			if (warn_about_nehahra_protocol)
			{
				Con_Warning ("nonstandard update bit, assuming Nehahra protocol\n");
				warn_about_nehahra_protocol = false;
			}

			a = MSG_ReadFloat();
			b = MSG_ReadFloat(); //alpha
			if (a == 2)
				MSG_ReadFloat(); //fullbright (not using this yet)
			ent->alpha = ENTALPHA_ENCODE(b);
		}
		else
			ent->alpha = ent->baseline.alpha;
	}
	//johnfitz

	//johnfitz -- moved here from above
	model = cl.model_precache[modnum];
	if (model != ent->model)
	{
		ent->model = model;
	// automatic animation (torches, etc) can be either all together
	// or randomized
		if (model)
		{
			if (model->synctype == ST_RAND)
				ent->syncbase = (float)(rand()&0x7fff) / 0x7fff;
			else
				ent->syncbase = 0.0;
		}
		else
			forcelink = true;	// hack to make null model players work
		if (num > 0 && num <= cl.maxclients)
			R_TranslateNewPlayerSkin (num - 1); //johnfitz -- was R_TranslatePlayerSkin

		ent->lerpflags |= LERP_RESETANIM; //johnfitz -- don't lerp animation across model changes
	}
	//johnfitz

	if ( forcelink )
	{	// didn't have an update last message
		VectorCopy (ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy (ent->msg_origins[0], ent->origin);
		VectorCopy (ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy (ent->msg_angles[0], ent->angles);
		ent->forcelink = true;
	}
}

/*
==================
CL_ParseBaseline
==================
*/
static void CL_ParseBaseline (entity_t *ent, int version) //johnfitz -- added argument
{
	int	i;
	int bits; //johnfitz

	if (version == 6)
	{
		CLFTE_ParseBaseline(&ent->baseline);
		return;
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	bits = (version == 2) ? MSG_ReadByte() : 0;
	ent->baseline.modelindex = (bits & B_LARGEMODEL) ? MSG_ReadShort() : MSG_ReadByte();
	ent->baseline.frame = (bits & B_LARGEFRAME) ? MSG_ReadShort() : MSG_ReadByte();
	//johnfitz

	ent->baseline.colormap = MSG_ReadByte();
	ent->baseline.skin = MSG_ReadByte();
	for (i = 0; i < 3; i++)
	{
		ent->baseline.origin[i] = MSG_ReadCoord (cl.protocolflags);
		ent->baseline.angles[i] = MSG_ReadAngle (cl.protocolflags);
	}

	ent->baseline.alpha = (bits & B_ALPHA) ? MSG_ReadByte() : ENTALPHA_DEFAULT; //johnfitz -- PROTOCOL_FITZQUAKE
}


/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
static void CL_ParseClientdata (void)
{
	int		i, j;
	int		bits; //johnfitz

	bits = (unsigned short)MSG_ReadShort (); //johnfitz -- read bits here isntead of in CL_ParseServerMessage()

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_EXTEND1)
		bits |= (MSG_ReadByte() << 16);
	if (bits & SU_EXTEND2)
		bits |= (MSG_ReadByte() << 24);
	//johnfitz

	if (bits & SU_VIEWHEIGHT)
		cl.stats[STAT_VIEWHEIGHT] = MSG_ReadChar ();
	else
		cl.stats[STAT_VIEWHEIGHT] = DEFAULT_VIEWHEIGHT;

	if (bits & SU_IDEALPITCH)
		cl.statsf[STAT_IDEALPITCH] = MSG_ReadChar ();
	else
		cl.statsf[STAT_IDEALPITCH] = 0;

	VectorCopy (cl.mvelocity[0], cl.mvelocity[1]);
	for (i = 0; i < 3; i++)
	{
		if (bits & (SU_PUNCH1<<i) )
			cl.punchangle[i] = MSG_ReadChar();
		else
			cl.punchangle[i] = 0;

		if (bits & (SU_VELOCITY1<<i) )
			cl.mvelocity[0][i] = MSG_ReadChar()*16;
		else
			cl.mvelocity[0][i] = 0;
	}

	//johnfitz -- update v_punchangles
	if (v_punchangles[0][0] != cl.punchangle[0] || v_punchangles[0][1] != cl.punchangle[1] || v_punchangles[0][2] != cl.punchangle[2])
	{
		VectorCopy (v_punchangles[0], v_punchangles[1]);
		VectorCopy (cl.punchangle, v_punchangles[0]);
	}
	//johnfitz

// [always sent]	if (bits & SU_ITEMS)
		cl.stats[STAT_ITEMS] = MSG_ReadLong ();

	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;

	if (bits & SU_WEAPONFRAME)
		cl.stats[STAT_WEAPONFRAME] = MSG_ReadByte ();
	else
		cl.stats[STAT_WEAPONFRAME] = 0;

	if (bits & SU_ARMOR)
		i = MSG_ReadByte ();
	else
		i = 0;
	if (cl.stats[STAT_ARMOR] != i)
	{
		cl.stats[STAT_ARMOR] = i;
		Sbar_Changed ();
	}

	if (bits & SU_WEAPON)
		i = MSG_ReadByte ();
	else
		i = 0;
	if (cl.stats[STAT_WEAPON] != i)
	{
		cl.stats[STAT_WEAPON] = i;
		Sbar_Changed ();
	}

	i = MSG_ReadShort ();
	if (cl.stats[STAT_HEALTH] != i)
	{
		cl.stats[STAT_HEALTH] = i;
		Sbar_Changed ();
	}

	i = MSG_ReadByte ();
	if (cl.stats[STAT_AMMO] != i)
	{
		cl.stats[STAT_AMMO] = i;
		Sbar_Changed ();
	}

	for (i = 0; i < 4; i++)
	{
		j = MSG_ReadByte ();
		if (cl.stats[STAT_SHELLS+i] != j)
		{
			cl.stats[STAT_SHELLS+i] = j;
			Sbar_Changed ();
		}
	}

	i = MSG_ReadByte ();

	if (standard_quake)
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != i)
		{
			cl.stats[STAT_ACTIVEWEAPON] = i;
			Sbar_Changed ();
		}
	}
	else
	{
		if (cl.stats[STAT_ACTIVEWEAPON] != (1<<i))
		{
			cl.stats[STAT_ACTIVEWEAPON] = (1<<i);
			Sbar_Changed ();
		}
	}

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (bits & SU_WEAPON2)
		cl.stats[STAT_WEAPON] |= (MSG_ReadByte() << 8);
	if (bits & SU_ARMOR2)
		cl.stats[STAT_ARMOR] |= (MSG_ReadByte() << 8);
	if (bits & SU_AMMO2)
		cl.stats[STAT_AMMO] |= (MSG_ReadByte() << 8);
	if (bits & SU_SHELLS2)
		cl.stats[STAT_SHELLS] |= (MSG_ReadByte() << 8);
	if (bits & SU_NAILS2)
		cl.stats[STAT_NAILS] |= (MSG_ReadByte() << 8);
	if (bits & SU_ROCKETS2)
		cl.stats[STAT_ROCKETS] |= (MSG_ReadByte() << 8);
	if (bits & SU_CELLS2)
		cl.stats[STAT_CELLS] |= (MSG_ReadByte() << 8);
	if (bits & SU_WEAPONFRAME2)
		cl.stats[STAT_WEAPONFRAME] |= (MSG_ReadByte() << 8);
	if (bits & SU_WEAPONALPHA)
		cl.viewent.alpha = MSG_ReadByte();
	else
		cl.viewent.alpha = ENTALPHA_DEFAULT;
	//johnfitz
    
	//johnfitz -- lerping
	//ericw -- this was done before the upper 8 bits of cl.stats[STAT_WEAPON] were filled in, breaking on large maps like zendar.bsp
	if (cl.viewent.model != cl.model_precache[cl.stats[STAT_WEAPON]])
	{
		cl.viewent.lerpflags |= LERP_RESETANIM; //don't lerp animation across model changes
	}
	//johnfitz
}

/*
=====================
CL_NewTranslation
=====================
*/
static void CL_NewTranslation (int slot)
{
	int		i, j;
	int		top, bottom;
	byte	*dest, *source;

	if (slot > cl.maxclients)
		Sys_Error ("CL_NewTranslation: slot > cl.maxclients");
	dest = cl.scores[slot].translations;
	source = vid.colormap;
	memcpy (dest, vid.colormap, sizeof(cl.scores[slot].translations));
	top = cl.scores[slot].colors & 0xf0;
	bottom = (cl.scores[slot].colors &15)<<4;
	R_TranslatePlayerSkin (slot);

	for (i = 0; i < VID_GRADES; i++, dest += 256, source+=256)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			memcpy (dest + TOP_RANGE, source + top, 16);
		else
		{
			for (j = 0; j < 16; j++)
				dest[TOP_RANGE+j] = source[top+15-j];
		}

		if (bottom < 128)
			memcpy (dest + BOTTOM_RANGE, source + bottom, 16);
		else
		{
			for (j = 0; j < 16; j++)
				dest[BOTTOM_RANGE+j] = source[bottom+15-j];
		}
	}
}

/*
=====================
CL_ParseStatic
=====================
*/
static void CL_ParseStatic (int version) //johnfitz -- added a parameter
{
	entity_t *ent;
	int		i;

	i = cl.num_statics;
	if (i >= MAX_STATIC_ENTITIES)
		Host_Error ("Too many static entities");

	ent = &cl_static_entities[i];
	cl.num_statics++;
	CL_ParseBaseline (ent, version); //johnfitz -- added second parameter

// copy it to the current state

	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->lerpflags |= LERP_RESETANIM; //johnfitz -- lerping
	ent->frame = ent->baseline.frame;

	ent->colormap = vid.colormap;
	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	ent->alpha = ent->baseline.alpha; //johnfitz -- alpha

	VectorCopy (ent->baseline.origin, ent->origin);
	VectorCopy (ent->baseline.angles, ent->angles);
	R_AddEfrags (ent);
}

/*
===================
CL_ParseStaticSound
===================
*/
static void CL_ParseStaticSound (int version) //johnfitz -- added argument
{
	vec3_t		org;
	int			sound_num, vol, atten;
	int			i;

	for (i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord (cl.protocolflags);

	//johnfitz -- PROTOCOL_FITZQUAKE
	if (version == 2)
		sound_num = MSG_ReadShort ();
	else
		sound_num = MSG_ReadByte ();
	//johnfitz

	vol = MSG_ReadByte ();
	atten = MSG_ReadByte ();

	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}

/*
CL_ParsePrecache

spike -- added this mostly for particle effects, but its also used for models+sounds (if needed)
*/
static void CL_ParsePrecache(void)
{
	unsigned short code = MSG_ReadShort();
	unsigned int index = code&0x3fff;
	const char *name = MSG_ReadString();
	switch((code>>14) & 0x3)
	{
	case 0:	//models
		if (index < MAX_MODELS)
		{
			cl.model_precache[index] = Mod_ForName (name, false);
			//FIXME: if its a bsp model, generate lightmaps.
		}
		break;
#ifdef PSET_SCRIPT
	case 1:	//particles
		if (index < MAX_PARTICLETYPES)
		{
			if (*name)
			{
				cl.particle_precache[index].name = strcpy(Hunk_Alloc(strlen(name)+1), name);
				cl.particle_precache[index].index = PScript_FindParticleType(cl.particle_precache[index].name);
			}
			else
			{
				cl.particle_precache[index].name = NULL;
				cl.particle_precache[index].index = -1;
			}
		}
		break;
#endif
	case 2:	//sounds
		if (index < MAX_SOUNDS)
			cl.sound_precache[index] = S_PrecacheSound (name);
		break;
//	case 3:	//unused
	default:
		Con_Warning("CL_ParsePrecache: unsupported precache type\n");
		break;
	}
}
#ifdef PSET_SCRIPT
/*
CL_RegisterParticles
called when the particle system has changed, and any cached indexes are now probably stale.
*/
void CL_RegisterParticles(void)
{
	extern qmodel_t	mod_known[];
	extern int		mod_numknown;
	int i;

	//make sure the precaches know the right effects
	for (i = 0; i < MAX_PARTICLETYPES; i++)
	{
		if (cl.particle_precache[i].name)
			cl.particle_precache[i].index = PScript_FindParticleType(cl.particle_precache[i].name);
		else
			cl.particle_precache[i].index = -1;
	}

	//and make sure models get the right effects+trails etc too
	for (i = 0; i < mod_numknown; i++)
		PScript_UpdateModelEffects(&mod_known[i]);
}

/*
CL_ParseParticles

spike -- this handles the various ssqc builtins (the ones that were based on csqc)
*/
static void CL_ParseParticles(int type)
{
	vec3_t org, vel;
	if (type < 0)
	{	//trail
		entity_t *ent;
		int entity = MSG_ReadShort();
		int efnum = MSG_ReadShort();
		org[0] = MSG_ReadCoord(cl.protocolflags);
		org[1] = MSG_ReadCoord(cl.protocolflags);
		org[2] = MSG_ReadCoord(cl.protocolflags);
		vel[0] = MSG_ReadCoord(cl.protocolflags);
		vel[1] = MSG_ReadCoord(cl.protocolflags);
		vel[2] = MSG_ReadCoord(cl.protocolflags);

		ent = CL_EntityNum(entity);

		if (efnum < MAX_PARTICLETYPES && cl.particle_precache[efnum].name)
			PScript_ParticleTrail(org, vel, cl.particle_precache[efnum].index, 0, NULL, &ent->trailstate);
	}
	else
	{	//point
		int efnum = MSG_ReadShort();
		int count;
		org[0] = MSG_ReadCoord(cl.protocolflags);
		org[1] = MSG_ReadCoord(cl.protocolflags);
		org[2] = MSG_ReadCoord(cl.protocolflags);
		if (type)
		{
			vel[0] = vel[1] = vel[2] = 0;
			count = 1;
		}
		else
		{
			vel[0] = MSG_ReadCoord(cl.protocolflags);
			vel[1] = MSG_ReadCoord(cl.protocolflags);
			vel[2] = MSG_ReadCoord(cl.protocolflags);
			count = MSG_ReadShort();
		}
		if (efnum < MAX_PARTICLETYPES && cl.particle_precache[efnum].name)
		{
			PScript_RunParticleEffectState (org, vel, count, cl.particle_precache[efnum].index, NULL);
		}
	}
}
#endif


#define SHOWNET(x) if(cl_shownet.value==2)Con_Printf ("%3i:%s\n", msg_readcount-1, x);

static void CL_ParseStatNumeric(int stat, int ival, float fval)
{
	if (stat < 0 || stat >= MAX_CL_STATS)
	{
		Con_Warning ("svc_updatestat: %i is invalid\n", stat);
		return;
	}
	cl.stats[stat] = ival;
	cl.statsf[stat] = fval;
	//just assume that they all affect the hud
	Sbar_Changed ();
}
static void CL_ParseStatFloat(int stat, float fval)
{
	CL_ParseStatNumeric(stat,fval,fval);
}
static void CL_ParseStatInt(int stat, int ival)
{
	CL_ParseStatNumeric(stat,ival,ival);
}
/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage (void)
{
	int			cmd;
	int			i;
	const char		*str; //johnfitz
	int			total, j, lastcmd; //johnfitz

//
// if recording demos, copy the message out
//
	if (cl_shownet.value == 1)
		Con_Printf ("%i ",net_message.cursize);
	else if (cl_shownet.value == 2)
		Con_Printf ("------------------\n");

	cl.onground = false;	// unless the server says otherwise
//
// parse the message
//
	MSG_BeginReading ();

	lastcmd = 0;
	while (1)
	{
		if (msg_badread)
			Host_Error ("CL_ParseServerMessage: Bad server message");

		cmd = MSG_ReadByte ();

		if (cmd == -1)
		{
			SHOWNET("END OF MESSAGE");

			if (cl.items != cl.stats[STAT_ITEMS])
			{
				for (i = 0; i < 32; i++)
					if ( (cl.stats[STAT_ITEMS] & (1<<i)) && !(cl.items & (1<<i)))
						cl.item_gettime[i] = cl.time;
				cl.items = cl.stats[STAT_ITEMS];
			}
			return;		// end of message
		}

	// if the high bit of the command byte is set, it is a fast update
		if (cmd & U_SIGNAL) //johnfitz -- was 128, changed for clarity
		{
			SHOWNET("fast update");
			CL_ParseUpdate (cmd&127);
			continue;
		}

		SHOWNET(svc_strings[cmd]);

	// other commands
		switch (cmd)
		{
		default:
			Host_Error ("Illegible server message, previous was %s", svc_strings[lastcmd]); //johnfitz -- added svc_strings[lastcmd]
			break;

		case svc_nop:
		//	Con_Printf ("svc_nop\n");
			break;

		case svc_time:
			cl.mtime[1] = cl.mtime[0];
			cl.mtime[0] = MSG_ReadFloat ();
			if (cl.protocol_pext2 & PEXT2_PREDINFO)
				MSG_ReadShort();	//input sequence ack.
			break;

		case svc_clientdata:
			CL_ParseClientdata (); //johnfitz -- removed bits parameter, we will read this inside CL_ParseClientdata()
			break;

		case svc_version:
			i = MSG_ReadLong ();
			//johnfitz -- support multiple protocols
			if (i != PROTOCOL_NETQUAKE && i != PROTOCOL_FITZQUAKE && i != PROTOCOL_RMQ)
				Host_Error ("Server returned version %i, not %i or %i or %i", i, PROTOCOL_NETQUAKE, PROTOCOL_FITZQUAKE, PROTOCOL_RMQ);
			cl.protocol = i;
			//johnfitz
			break;

		case svc_disconnect:
			Host_EndGame ("Server disconnected\n");

		case svc_print:
			Con_Printf ("%s", MSG_ReadString ());
			break;

		case svc_centerprint:
			//johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			//johnfitz
			break;

		case svc_stufftext:
			Cbuf_AddText (MSG_ReadString ());
			break;

		case svc_damage:
			V_ParseDamage ();
			break;

		case svc_serverinfo:
			CL_ParseServerInfo ();
			vid.recalc_refdef = true;	// leave intermission full screen
			break;

		case svc_setangle:
			for (i=0 ; i<3 ; i++)
				cl.viewangles[i] = MSG_ReadAngle (cl.protocolflags);
			break;

		case svc_setview:
			cl.viewentity = MSG_ReadShort ();
			break;

		case svc_lightstyle:
			i = MSG_ReadByte ();
			str = MSG_ReadString();
			if ((unsigned)i < MAX_LIGHTSTYLES)
			{
				q_strlcpy (cl_lightstyle[i].map, str, MAX_STYLESTRING);
				cl_lightstyle[i].length = Q_strlen(cl_lightstyle[i].map);
				//johnfitz -- save extra info
				if (cl_lightstyle[i].length)
				{
					total = 0;
					cl_lightstyle[i].peak = 'a';
					for (j=0; j<cl_lightstyle[i].length; j++)
					{
						total += cl_lightstyle[i].map[j] - 'a';
						cl_lightstyle[i].peak = q_max(cl_lightstyle[i].peak, cl_lightstyle[i].map[j]);
					}
					cl_lightstyle[i].average = total / cl_lightstyle[i].length + 'a';
				}
				else
					cl_lightstyle[i].average = cl_lightstyle[i].peak = 'm';
			}
			//johnfitz
			break;

		case svc_sound:
			CL_ParseStartSoundPacket();
			break;

		case svc_stopsound:
			i = MSG_ReadShort();
			S_StopSound(i>>3, i&7);
			break;

		case svc_updatename:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
			q_strlcpy (cl.scores[i].name, MSG_ReadString(), MAX_SCOREBOARDNAME);
			break;

		case svc_updatefrags:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
			cl.scores[i].frags = MSG_ReadShort ();
			break;

		case svc_updatecolors:
			Sbar_Changed ();
			i = MSG_ReadByte ();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
			cl.scores[i].colors = MSG_ReadByte ();
			CL_NewTranslation (i);
			break;

		case svc_particle:
			R_ParseParticleEffect ();
			break;

		case svc_spawnbaseline:
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 1); // johnfitz -- added second parameter
			break;

		case svc_spawnstatic:
			CL_ParseStatic (1); //johnfitz -- added parameter
			break;

		case svc_temp_entity:
			CL_ParseTEnt ();
			break;

		case svc_setpause:
			cl.paused = MSG_ReadByte ();
			if (cl.paused)
			{
				CDAudio_Pause ();
				BGM_Pause ();
			}
			else
			{
				CDAudio_Resume ();
				BGM_Resume ();
			}
			break;

		case svc_signonnum:
			i = MSG_ReadByte ();
			if (i <= cls.signon)
				Host_Error ("Received signon %i when at %i", i, cls.signon);
			cls.signon = i;
			//johnfitz -- if signonnum==2, signon packet has been fully parsed, so check for excessive static ents and efrags
			if (i == 2)
			{
				if (cl.num_statics > 128)
					Con_DWarning ("%i static entities exceeds standard limit of 128.\n", cl.num_statics);
				R_CheckEfrags ();
			}
			//johnfitz
			CL_SignonReply ();
			break;

		case svc_killedmonster:
			cl.stats[STAT_MONSTERS]++;
			break;

		case svc_foundsecret:
			cl.stats[STAT_SECRETS]++;
			break;

		case svc_updatestat:
			i = MSG_ReadByte ();
			CL_ParseStatInt(i, MSG_ReadLong());
			break;

		case svc_spawnstaticsound:
			CL_ParseStaticSound (1); //johnfitz -- added parameter
			break;

		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte ();
			cl.looptrack = MSG_ReadByte ();
			if ( (cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1) )
				BGM_PlayCDtrack ((byte)cls.forcetrack, true);
			else
				BGM_PlayCDtrack ((byte)cl.cdtrack, true);
			break;

		case svc_intermission:
			cl.intermission = 1;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			break;

		case svc_finale:
			cl.intermission = 2;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			//johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			//johnfitz
			break;

		case svc_cutscene:
			cl.intermission = 3;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true;	// go to full screen
			//johnfitz -- log centerprints to console
			str = MSG_ReadString ();
			SCR_CenterPrint (str);
			Con_LogCenterPrint (str);
			//johnfitz
			break;

		case svc_sellscreen:
			Cmd_ExecuteString ("help", src_command);
			break;

		//johnfitz -- new svc types
		case svc_skybox:
			Sky_LoadSkyBox (MSG_ReadString());
			break;

		case svc_bf:
			Cmd_ExecuteString ("bf", src_command);
			break;

		case svc_fog:
			Fog_ParseServerMessage ();
			break;

		case svc_spawnbaseline2: //PROTOCOL_FITZQUAKE
			i = MSG_ReadShort ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 2);
			break;

		case svc_spawnstatic2: //PROTOCOL_FITZQUAKE
			CL_ParseStatic (2);
			break;

		case svc_spawnstaticsound2: //PROTOCOL_FITZQUAKE
			CL_ParseStaticSound (2);
			break;
		//johnfitz

		//spike -- for particles more than anything else
		case svcdp_precache:
			CL_ParsePrecache();
			break;
#ifdef PSET_SCRIPT
		case svcdp_trailparticles:
			CL_ParseParticles(-1);
			break;
		case svcdp_pointparticles:
			CL_ParseParticles(0);
			break;
		case svcdp_pointparticles1:
			CL_ParseParticles(1);
			break;
#endif

		//spike -- new deltas (including new fields etc)
		//stats also changed, and are sent unreliably using the same ack mechanism (which means they're not blocked until the reliables are acked, preventing the need to spam them in every packet).
		case svcdp_updatestatbyte:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcdp_updatestatbyte but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatInt(i, MSG_ReadByte());
			break;
		case svcfte_updatestatstring:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updatestatstring but extension not active");
			i = MSG_ReadByte ();
			if (i >= 0 && i < MAX_CL_STATS)
				/*cl.statss[i] =*/ MSG_ReadString ();
			else
				Con_Warning ("svcfte_updatestatstring: %i is invalid\n", i);
			break;
		case svcfte_updatestatfloat:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updatestatfloat but extension not active");
			i = MSG_ReadByte ();
			CL_ParseStatFloat(i, MSG_ReadFloat());
			break;
		//static ents get all the new fields too, even if the client will probably ignore most of them, the option is at least there to fix it without updating protocols separately.
		case svcfte_spawnstatic2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnstatic2 but extension not active");
			CL_ParseStatic (6);
			break;
		//baselines have all fields. hurrah for the same delta mechanism
		case svcfte_spawnbaseline2:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_spawnbaseline2 but extension not active");
			i = MSG_ReadEntity ();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline (CL_EntityNum(i), 6);
			break;
		//ent updates replace svc_time too
		case svcfte_updateentities:
			if (!(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
				Host_Error ("Received svcfte_updateentities but extension not active");
			CLFTE_ParseEntitiesUpdate();
			break;
		//spike
		}

		lastcmd = cmd; //johnfitz
	}
}

