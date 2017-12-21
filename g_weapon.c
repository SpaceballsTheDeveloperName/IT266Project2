#include "g_local.h"


/*
=================
check_dodge

This is a support routine used when a client is firing
a non-instant attack weapon.  It checks to see if a
monster's dodge function should be called.
=================
*/
static void check_dodge (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	vec3_t	end;
	vec3_t	v;
	trace_t	tr;
	float	eta;

	// easy mode only ducks one quarter the time
	if (skill->value == 0)
	{
		if (random() > 0.25)
			return;
	}
	VectorMA (start, 8192, dir, end);
	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
	if ((tr.ent) && (tr.ent->svflags & SVF_MONSTER) && (tr.ent->health > 0) && (tr.ent->monsterinfo.dodge) && infront(tr.ent, self))
	{
		VectorSubtract (tr.endpos, start, v);
		eta = (VectorLength(v) - tr.ent->maxs[0]) / speed;
		tr.ent->monsterinfo.dodge (tr.ent, self, eta);
	}
}


/*
=================
fire_hit

Used for all impact (hit/punch/slash) attacks
=================
*/
qboolean fire_hit (edict_t *self, vec3_t aim, int damage, int kick)
{
	trace_t		tr;
	vec3_t		forward, right, up;
	vec3_t		v;
	vec3_t		point;
	float		range;
	vec3_t		dir;

	//see if enemy is in range
	VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
	range = VectorLength(dir);
	if (range > aim[0])
		return false;

	if (aim[1] > self->mins[0] && aim[1] < self->maxs[0])
	{
		// the hit is straight on so back the range up to the edge of their bbox
		range -= self->enemy->maxs[0];
	}
	else
	{
		// this is a side hit so adjust the "right" value out to the edge of their bbox
		if (aim[1] < 0)
			aim[1] = self->enemy->mins[0];
		else
			aim[1] = self->enemy->maxs[0];
	}

	VectorMA (self->s.origin, range, dir, point);

	tr = gi.trace (self->s.origin, NULL, NULL, point, self, MASK_SHOT);
	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		// if it will hit any client/monster then hit the one we wanted to hit
		if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
			tr.ent = self->enemy;
	}

	AngleVectors(self->s.angles, forward, right, up);
	VectorMA (self->s.origin, range, forward, point);
	VectorMA (point, aim[1], right, point);
	VectorMA (point, aim[2], up, point);
	VectorSubtract (point, self->enemy->s.origin, dir);

	// do the damage
	T_Damage (tr.ent, self, self, dir, point, vec3_origin, damage, kick/2, DAMAGE_NO_KNOCKBACK, MOD_HIT);

	if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
		return false;

	// do our special form of knockback here
	VectorMA (self->enemy->absmin, 0.5, self->enemy->size, v);
	VectorSubtract (v, point, v);
	VectorNormalize (v);
	VectorMA (self->enemy->velocity, kick, v, self->enemy->velocity);
	if (self->enemy->velocity[2] > 0)
		self->enemy->groundentity = NULL;
	return true;
}


/*
=================
fire_lead

This is an internal support routine used for bullet/pellet based weapons.
=================
*/
static void fire_lead (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int te_impact, int hspread, int vspread, int mod)
{
	trace_t		tr;
	vec3_t		dir;
	vec3_t		forward, right, up;
	vec3_t		end;
	float		r;
	float		u;
	vec3_t		water_start;
	qboolean	water = false;
	int			content_mask = MASK_SHOT | MASK_WATER;

	tr = gi.trace (self->s.origin, NULL, NULL, start, self, MASK_SHOT);
	if (!(tr.fraction < 1.0))
	{
		vectoangles (aimdir, dir);
		AngleVectors (dir, forward, right, up);

		r = crandom()*hspread;
		u = crandom()*vspread;
		VectorMA (start, 8192, forward, end);
		VectorMA (end, r, right, end);
		VectorMA (end, u, up, end);

		if (gi.pointcontents (start) & MASK_WATER)
		{
			water = true;
			VectorCopy (start, water_start);
			content_mask &= ~MASK_WATER;
		}

		tr = gi.trace (start, NULL, NULL, end, self, content_mask);

		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int		color;

			water = true;
			VectorCopy (tr.endpos, water_start);

			if (!VectorCompare (start, tr.endpos))
			{
				if (tr.contents & CONTENTS_WATER)
				{
					if (strcmp(tr.surface->name, "*brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents & CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents & CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (TE_SPLASH);
					gi.WriteByte (8);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.WriteByte (color);
					gi.multicast (tr.endpos, MULTICAST_PVS);
				}

				// change bullet's course when it enters water
				VectorSubtract (end, start, dir);
				vectoangles (dir, dir);
				AngleVectors (dir, forward, right, up);
				r = crandom()*hspread*2;
				u = crandom()*vspread*2;
				VectorMA (water_start, 8192, forward, end);
				VectorMA (end, r, right, end);
				VectorMA (end, u, up, end);
			}

			// re-trace ignoring water this time
			tr = gi.trace (water_start, NULL, NULL, end, self, MASK_SHOT);
		}
	}

	// send gun puff / flash
	if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		if (tr.fraction < 1.0)
		{
			if (tr.ent->takedamage)
			{
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_BULLET, mod);
			}
			else
			{
				if (strncmp (tr.surface->name, "sky", 3) != 0)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (te_impact);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.multicast (tr.endpos, MULTICAST_PVS);

					if (self->client)
						PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
				}
			}
		}
	}

	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t	pos;

		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}
}


/*
=================
fire_bullet

Fires a single round.  Used for machinegun and chaingun.  Would be fine for
pistols, rifles, etc....
=================
*/
void fire_bullet (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int mod)
{
	fire_lead (self, start, aimdir, damage, kick, TE_GUNSHOT, hspread, vspread, mod);
}


/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
=================
*/
void fire_shotgun (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count, int mod)
{
	int		i;

	for (i = 0; i < count; i++)
		fire_lead (self, start, aimdir, damage, kick, TE_SHOTGUN, hspread, vspread, mod);
}


/*
=================
fire_blaster

Fires a single blaster bolt.  Used by the blaster and hyper blaster.
=================
*/
void blaster_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int		mod;

	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (self->spawnflags & 1)
			mod = MOD_HYPERBLASTER;
		else
			mod = MOD_BLASTER;
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, self->dmg, 1, DAMAGE_ENERGY, mod);
	}
	else
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BLASTER);
		gi.WritePosition (self->s.origin);
		if (!plane)
			gi.WriteDir (vec3_origin);
		else
			gi.WriteDir (plane->normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict (self);
}

void fire_blaster (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	// yes, I know it looks weird that projectiles are deadmonsters
	// what this means is that when prediction is used against the object
	// (blaster/hyperblaster shots), the player won't be solid clipped against
	// the object.  Right now trying to run into a firing hyperblaster
	// is very jerky since you are predicted 'against' the shots.
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blaster_touch;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	if (hyper)
		bolt->spawnflags = 1;
	gi.linkentity (bolt);

	if (self->client)
		check_dodge (self, bolt->s.origin, dir, speed);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}	


/*
=================
fire_grenade
=================
*/
static void Grenade_Explode (edict_t *ent)
{
	vec3_t		origin;
	int			mod;

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	//FIXME: if we are onground then raise our Z just a bit since we are a point?
	if (ent->enemy)
	{
		float	points;
		vec3_t	v;
		vec3_t	dir;

		VectorAdd (ent->enemy->mins, ent->enemy->maxs, v);
		VectorMA (ent->enemy->s.origin, 0.5, v, v);
		VectorSubtract (ent->s.origin, v, v);
		points = ent->dmg - 0.5 * VectorLength (v);
		VectorSubtract (ent->enemy->s.origin, ent->s.origin, dir);
		if (ent->spawnflags & 1)
			mod = MOD_HANDGRENADE;
		else
			mod = MOD_GRENADE;
		T_Damage (ent->enemy, ent, ent->owner, dir, ent->s.origin, vec3_origin, (int)points, (int)points, DAMAGE_RADIUS, mod);
	}

	if (ent->spawnflags & 2)
		mod = MOD_HELD_GRENADE;
	else if (ent->spawnflags & 1)
		mod = MOD_HG_SPLASH;
	else
		mod = MOD_G_SPLASH;
	T_RadiusDamage(ent, ent->owner, ent->dmg, ent->enemy, ent->dmg_radius, mod);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

static void Grenade_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (!other->takedamage)
	{
		if (ent->spawnflags & 1)
		{
			if (random() > 0.5)
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
		}
		else
		{
			gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/grenlb1b.wav"), 1, ATTN_NORM, 0);
		}
		return;
	}

	ent->enemy = other;
	Grenade_Explode (ent);
}

void fire_grenade (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->nextthink = level.time + timer;
	grenade->think = Grenade_Explode;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "grenade";

	gi.linkentity (grenade);
}

void fire_grenade2 (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius, qboolean held)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade2/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->nextthink = level.time + timer;
	grenade->think = Grenade_Explode;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "hgrenade";
	if (held)
		grenade->spawnflags = 3;
	else
		grenade->spawnflags = 1;
	grenade->s.sound = gi.soundindex("weapons/hgrenc1b.wav");

	if (timer <= 0.0)
		Grenade_Explode (grenade);
	else
	{
		gi.sound (self, CHAN_WEAPON, gi.soundindex ("weapons/hgrent1a.wav"), 1, ATTN_NORM, 0);
		gi.linkentity (grenade);
	}
}


/*
=================
fire_rocket
=================
*/
void rocket_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		origin;
	int			n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, MOD_ROCKET);
	}
	else
	{
		// don't throw any debris in net games
		if (!deathmatch->value && !coop->value)
		{
			if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
			{
				n = rand() % 5;
				while(n--)
					ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
			}
		}
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

//homing missles
void homing_think(edict_t *ent)
{
	edict_t	*target = NULL;
	edict_t *blip = NULL;
	vec3_t	targetdir, blipdir;
	vec_t	speed;

	while ((blip = findradius(blip, ent->s.origin, 1000)) != NULL)
	{
		if (!(blip->svflags & SVF_MONSTER) && !blip->client)
			continue;
		if (blip == ent->owner)
			continue;
		if (!blip->takedamage)
			continue;
		if (blip->health <= 0)
			continue;
		if (!visible(ent, blip))
			continue;
		if (!infront(ent, blip))
			continue;
		VectorSubtract(blip->s.origin, ent->s.origin, blipdir);
		blipdir[2] += 16;
		if ((target == NULL) || (VectorLength(blipdir) < VectorLength(targetdir)))
			{
				target = blip;
				VectorCopy(blipdir, targetdir);
			}
		
	}
	if (target != NULL)
		{
			// target acquired, nudge our direction toward it
			VectorNormalize(targetdir);
			VectorScale(targetdir, 0.2, targetdir);
			VectorAdd(targetdir, ent->movedir, targetdir);
			VectorNormalize(targetdir);
			VectorCopy(targetdir, ent->movedir);
			vectoangles(targetdir, ent->s.angles);
			speed = VectorLength(ent->velocity);
			VectorScale(targetdir, speed, ent->velocity);
		}
	
		ent->nextthink = level.time + .1;

}

void fire_rocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*rocket;

	rocket = G_Spawn();
	VectorCopy (start, rocket->s.origin);
	VectorCopy (dir, rocket->movedir);
	vectoangles (dir, rocket->s.angles);
	VectorScale (dir, speed, rocket->velocity);
	rocket->movetype = MOVETYPE_FLYMISSILE;
	rocket->clipmask = MASK_SHOT;
	rocket->solid = SOLID_BBOX;
	rocket->s.effects |= EF_ROCKET;
	VectorClear (rocket->mins);
	VectorClear (rocket->maxs);
	rocket->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	rocket->owner = self;
	rocket->touch = rocket_touch;
	//removed speed
	//removed G_FreeEdict
	//see if homing is on
	if (self->client && self->client->pers.homing_state)
		{
			// CCH: if they have 5 cells, start homing, otherwise normal rocket think
			if (self->client->pers.inventory[ITEM_INDEX(FindItem("Cells"))] >= 5)
			{
			self->client->pers.inventory[ITEM_INDEX(FindItem("Cells"))] -= 5;
			rocket->nextthink = level.time + .1;
			rocket->think = homing_think;
			}
			else {
				gi.cprintf(self, PRINT_HIGH, "No cells for homing missile.\n");
				rocket->nextthink = level.time + 8000 / speed;
				rocket->think = G_FreeEdict;
			
				 }
			}
	else {
		rocket->nextthink = level.time + 8000 / speed;
		rocket->think = G_FreeEdict;
		
		 }

	rocket->dmg = damage;
	rocket->radius_dmg = radius_damage;
	rocket->dmg_radius = damage_radius;
	rocket->s.sound = gi.soundindex ("weapons/rockfly.wav");
	rocket->classname = "rocket";

	if (self->client)
		check_dodge (self, rocket->s.origin, dir, speed);

	gi.linkentity (rocket);
}


/*
=================
fire_rail
=================
*/
void fire_rail (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{
	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;
	int			mask;
	qboolean	water;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;
	while (ignore)
	{
		tr = gi.trace (from, NULL, NULL, end, ignore, mask);

		if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
		{
			mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
			water = true;
		}
		else
		{
			//ZOID--added so rail goes through SOLID_BBOX entities (gibs, etc)
			if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client) ||
				(tr.ent->solid == SOLID_BBOX))
				ignore = tr.ent;
			else
				ignore = NULL;

			if ((tr.ent != self) && (tr.ent->takedamage))
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_RAILGUN);
		}

		VectorCopy (tr.endpos, from);
	}

	// send gun puff / flash
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_RAILTRAIL);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);
//	gi.multicast (start, MULTICAST_PHS);
	if (water)
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_RAILTRAIL);
		gi.WritePosition (start);
		gi.WritePosition (tr.endpos);
		gi.multicast (tr.endpos, MULTICAST_PHS);
	}

	if (self->client)
		PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
}


/*
=================
fire_bfg
=================
*/
void bfg_explode (edict_t *self)
{
	edict_t	*ent;
	float	points;
	vec3_t	v;
	float	dist;

	if (self->s.frame == 0)
	{
		// the BFG effect
		ent = NULL;
		while ((ent = findradius(ent, self->s.origin, self->dmg_radius)) != NULL)
		{
			if (!ent->takedamage)
				continue;
			if (ent == self->owner)
				continue;
			if (!CanDamage (ent, self))
				continue;
			if (!CanDamage (ent, self->owner))
				continue;

			VectorAdd (ent->mins, ent->maxs, v);
			VectorMA (ent->s.origin, 0.5, v, v);
			VectorSubtract (self->s.origin, v, v);
			dist = VectorLength(v);
			points = self->radius_dmg * (1.0 - sqrt(dist/self->dmg_radius));
			if (ent == self->owner)
				points = points * 0.5;

			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_BFG_EXPLOSION);
			gi.WritePosition (ent->s.origin);
			gi.multicast (ent->s.origin, MULTICAST_PHS);
			T_Damage (ent, self, self->owner, self->velocity, ent->s.origin, vec3_origin, (int)points, 0, DAMAGE_ENERGY, MOD_BFG_EFFECT);
		}
	}

	self->nextthink = level.time + FRAMETIME;
	self->s.frame++;
	if (self->s.frame == 5)
		self->think = G_FreeEdict;
}

void bfg_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	// core explosion - prevents firing it into the wall/floor
	if (other->takedamage)
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, 200, 0, 0, MOD_BFG_BLAST);
	T_RadiusDamage(self, self->owner, 200, other, 100, MOD_BFG_BLAST);

	gi.sound (self, CHAN_VOICE, gi.soundindex ("weapons/bfg__x1b.wav"), 1, ATTN_NORM, 0);
	self->solid = SOLID_NOT;
	self->touch = NULL;
	VectorMA (self->s.origin, -1 * FRAMETIME, self->velocity, self->s.origin);
	VectorClear (self->velocity);
	self->s.modelindex = gi.modelindex ("sprites/s_bfg3.sp2");
	self->s.frame = 0;
	self->s.sound = 0;
	self->s.effects &= ~EF_ANIM_ALLFAST;
	self->think = bfg_explode;
	self->nextthink = level.time + FRAMETIME;
	self->enemy = other;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BFG_BIGEXPLOSION);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PVS);
}


void bfg_think (edict_t *self)
{
	edict_t	*ent;
	edict_t	*ignore;
	vec3_t	point;
	vec3_t	dir;
	vec3_t	start;
	vec3_t	end;
	int		dmg;
	trace_t	tr;

	if (deathmatch->value)
		dmg = 5;
	else
		dmg = 10;

	ent = NULL;
	while ((ent = findradius(ent, self->s.origin, 256)) != NULL)
	{
		if (ent == self)
			continue;

		if (ent == self->owner)
			continue;

		if (!ent->takedamage)
			continue;

		if (!(ent->svflags & SVF_MONSTER) && (!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
			continue;

		VectorMA (ent->absmin, 0.5, ent->size, point);

		VectorSubtract (point, self->s.origin, dir);
		VectorNormalize (dir);

		ignore = self;
		VectorCopy (self->s.origin, start);
		VectorMA (start, 2048, dir, end);
		while(1)
		{
			tr = gi.trace (start, NULL, NULL, end, ignore, CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_DEADMONSTER);

			if (!tr.ent)
				break;

			// hurt it if we can
			if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER) && (tr.ent != self->owner))
				T_Damage (tr.ent, self, self->owner, dir, tr.endpos, vec3_origin, dmg, 1, DAMAGE_ENERGY, MOD_BFG_LASER);

			// if we hit something that's not a monster or player we're done
			if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
			{
				gi.WriteByte (svc_temp_entity);
				gi.WriteByte (TE_LASER_SPARKS);
				gi.WriteByte (4);
				gi.WritePosition (tr.endpos);
				gi.WriteDir (tr.plane.normal);
				gi.WriteByte (self->s.skinnum);
				gi.multicast (tr.endpos, MULTICAST_PVS);
				break;
			}

			ignore = tr.ent;
			VectorCopy (tr.endpos, start);
		}

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BFG_LASER);
		gi.WritePosition (self->s.origin);
		gi.WritePosition (tr.endpos);
		gi.multicast (self->s.origin, MULTICAST_PHS);
	}

	self->nextthink = level.time + FRAMETIME;
}


void fire_bfg (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius)
{
	edict_t	*bfg;

	bfg = G_Spawn();
	VectorCopy (start, bfg->s.origin);
	VectorCopy (dir, bfg->movedir);
	vectoangles (dir, bfg->s.angles);
	VectorScale (dir, speed, bfg->velocity);
	bfg->movetype = MOVETYPE_FLYMISSILE;
	bfg->clipmask = MASK_SHOT;
	bfg->solid = SOLID_BBOX;
	bfg->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (bfg->mins);
	VectorClear (bfg->maxs);
	bfg->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	bfg->owner = self;
	bfg->touch = bfg_touch;
	bfg->nextthink = level.time + 8000/speed;
	bfg->think = G_FreeEdict;
	bfg->radius_dmg = damage;
	bfg->dmg_radius = damage_radius;
	bfg->classname = "bfg blast";
	bfg->s.sound = gi.soundindex ("weapons/bfg__l1a.wav");

	bfg->think = bfg_think;
	bfg->nextthink = level.time + FRAMETIME;
	bfg->teammaster = bfg;
	bfg->teamchain = NULL;

	if (self->client)
		check_dodge (self, bfg->s.origin, dir, speed);

	gi.linkentity (bfg);
}

// edict->hookstate_ bit constants
#define HOOK_ON		0x00000001		// set if hook command is active
#define HOOK_IN		0x00000002		// set if hook has attached
#define SHRINK_ON	0x00000004		// set if shrink chain is active 
#define GROW_ON		0x00000008		// set if grow chain is active

// edict->sounds constants
#define MOTOR_OFF	0		// motor sound has not been triggered
#define MOTOR_START	1		// motor start sound has been triggered
#define MOTOR_ON	2		// motor running sound has been triggered

// this is the same as function P_ProjectSource in p_weapons.c except it projects
// the offset distance in reverse since hook is launched with player's free hand
void P_ProjectSource_Reverse(gclient_t *client, vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	vec3_t	_distance;

	VectorCopy(distance, _distance);
	if (client->pers.hand == RIGHT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;
	G_ProjectSource(point, _distance, forward, right, result);
}


void DropHook(edict_t *ent)
{
	// enable prediction back in case it was left disabled
	ent->owner->client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;

	// remove all hook flags
	ent->owner->client->hookstate_ = 0;

	gi.sound(ent->owner, CHAN_HOOK, gi.soundindex("hook/retract.wav"), 1, ATTN_IDLE, 0);

	// removes hook
	G_FreeEdict(ent);
}


void MaintainLinks(edict_t *ent)
{
	float multiplier;		// prediction multiplier
	vec3_t pred_hookpos;	// predicted future hook origin
	vec3_t norm_hookvel;	// normalized hook velocity

	vec3_t	offset, start;
	vec3_t	forward, right;

	vec3_t chainvec;		// vector of the chain 
	vec3_t norm_chainvec;	// vector of chain with distance of 1

	// predicts hook's future position since chain links fall behind
	multiplier = VectorLength(ent->velocity) / 22;
	VectorNormalize2(ent->velocity, norm_hookvel);
	VectorMA(ent->s.origin, multiplier, norm_hookvel, pred_hookpos);

	// derive start point of chain
	AngleVectors(ent->owner->client->v_angle, forward, right, NULL);
	VectorSet(offset, 8, 8, ent->owner->viewheight - 8);
	P_ProjectSource_Reverse(ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

	// get info about chain
	_VectorSubtract(pred_hookpos, start, chainvec);
	VectorNormalize2(chainvec, norm_chainvec);

	// shorten ends of chain
	VectorMA(start, 10, norm_chainvec, start);
	VectorMA(pred_hookpos, -20, norm_chainvec, pred_hookpos);

	// create temp entity chain
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_MEDIC_CABLE_ATTACK);
	gi.WriteShort(ent - g_edicts);
	gi.WritePosition(pred_hookpos);
	gi.WritePosition(start);
	gi.multicast(ent->s.origin, MULTICAST_PVS);
}


void HookBehavior(edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	vec3_t	chainvec;		// chain's vector
	float chainlen;			// length of extended chain
	vec3_t velpart;			// player's velocity component moving to or away from hook
	float force;			// restrainment force
	qboolean chain_moving;

	// decide when to disconnect hook
	if ((!(ent->owner->client->hookstate_ & HOOK_ON)) ||// if hook has been retracted
		(ent->enemy->solid == SOLID_NOT) ||			// if target is no longer solid (i.e. hook broke glass; exploded barrels, gibs) 
		(ent->owner->deadflag) ||						// if player died
		(ent->owner->s.event == EV_PLAYER_TELEPORT))	// if player goes through teleport
	{
		DropHook(ent);
		return;
	}

	// gives hook same velocity as the entity it is stuck in
	VectorCopy(ent->enemy->velocity, ent->velocity);

	// chain sizing 

	chain_moving = false;

	// grow the length of the chain
	if ((ent->owner->client->hookstate_ & GROW_ON) && (ent->angle < hook_max_len->value))
	{
		ent->angle += hook_rpf->value;
		if (ent->angle > hook_max_len->value) ent->angle = hook_max_len->value;
		chain_moving = true;
	}

	// shrink the length of the chain
	if ((ent->owner->client->hookstate_ & SHRINK_ON) && (ent->angle > hook_min_len->value))
	{
		ent->angle -= hook_rpf->value;
		if (ent->angle < hook_min_len->value) ent->angle = hook_min_len->value;
		chain_moving = true;
	}

	// determine sound play if climbing or sliding
	if (chain_moving)
	{
		// play start of climb sound
		if (ent->sounds == MOTOR_OFF)
		{
			gi.sound(ent->owner, CHAN_HOOK, gi.soundindex("hook/motor1.wav"), 1, ATTN_IDLE, 0);
			ent->sounds = MOTOR_START;
		}
		// play repetitive climb sound
		else if (ent->sounds == MOTOR_START)
		{
			gi.sound(ent->owner, CHAN_HOOK, gi.soundindex("hook/motor2.wav"), 1, ATTN_IDLE, 0);
			ent->sounds = MOTOR_ON;
		}
	}
	else if (ent->sounds != MOTOR_OFF)
	{
		gi.sound(ent->owner, CHAN_HOOK, gi.soundindex("hook/motor3.wav"), 1, ATTN_IDLE, 0);
		ent->sounds = MOTOR_OFF;
	}

	// chain physics

	// derive start point of chain
	AngleVectors(ent->owner->client->v_angle, forward, right, NULL);
	VectorSet(offset, 8, 8, ent->owner->viewheight - 8);
	P_ProjectSource_Reverse(ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

	// get info about chain
	_VectorSubtract(ent->s.origin, start, chainvec);
	chainlen = VectorLength(chainvec);

	// if player's location is beyond the chain's reach
	if (chainlen > ent->angle)
	{
		// determine player's velocity component of chain vector
		VectorScale(chainvec, _DotProduct(ent->owner->velocity, chainvec) / _DotProduct(chainvec, chainvec), velpart);

		// restrainment default force 
		force = (chainlen - ent->angle) * 5;

		// if player's velocity heading is away from the hook
		if (_DotProduct(ent->owner->velocity, chainvec) < 0)
		{
			// if chain has streched for 25 units
			if (chainlen > ent->angle + 25)
				// remove player's velocity component moving away from hook
				_VectorSubtract(ent->owner->velocity, velpart, ent->owner->velocity);
		}
		else  // if player's velocity heading is towards the hook
		{
			if (VectorLength(velpart) < force)
				force -= VectorLength(velpart);
			else
				force = 0;
		}
	}
	else
		force = 0;

	// disable prediction while suspended in air by hook
	// if server console variable hook_no_pred is set 
	if (!(ent->owner->client->ps.pmove.pm_flags & PMF_ON_GROUND))
	{
		if (hook_no_pred->value)
			ent->owner->client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
	}
	else
		ent->owner->client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;

	// applys chain restrainment 
	VectorNormalize(chainvec);
	VectorMA(ent->owner->velocity, force, chainvec, ent->owner->velocity);

	MaintainLinks(ent);

	// prep for next think
	ent->nextthink = level.time + FRAMETIME;
}


void HookTouch(edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	vec3_t	chainvec;		// chain's vector

	// derive start point of chain
	AngleVectors(ent->owner->client->v_angle, forward, right, NULL);
	VectorSet(offset, 8, 8, ent->owner->viewheight - 8);
	P_ProjectSource_Reverse(ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

	// member angle is used to store the length of the chain
	_VectorSubtract(ent->s.origin, start, chainvec);
	ent->angle = VectorLength(chainvec);

	// don't attach hook to sky
	if (surf && (surf->flags & SURF_SKY))
	{
		DropHook(ent);
		return;
	}

	// inflict damage on damageable items
	if (other->takedamage)
		T_Damage(other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 100, 0, MOD_HIT);

	if (other->solid == SOLID_BBOX)
	{
		if ((other->svflags & SVF_MONSTER) || (other->client))
			gi.sound(ent, CHAN_VOICE, gi.soundindex("hook/smack.wav"), 1, ATTN_IDLE, 0);

		DropHook(ent);
		return;
	}

	if (other->solid == SOLID_BSP)
	{
		// create puff of smoke
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_SHOTGUN);
		gi.WritePosition(ent->s.origin);
		if (!plane)
			gi.WriteDir(vec3_origin);
		else
			gi.WriteDir(plane->normal);
		gi.multicast(ent->s.origin, MULTICAST_PVS);

		gi.sound(ent, CHAN_VOICE, gi.soundindex("hook/hit.wav"), 1, ATTN_IDLE, 0);
		VectorClear(ent->avelocity);
	}
	else if (other->solid == SOLID_TRIGGER)
	{
		// debugging line; don't know if this will ever happen 
		gi.cprintf(ent->owner, PRINT_HIGH, "Hook touched a SOLID_TRIGGER\n");
	}

	// hook gets the same velocity as the item it attached to
	VectorCopy(other->velocity, ent->velocity);

	// flags hook as being attached to something
	ent->owner->client->hookstate_ |= HOOK_IN;

	ent->enemy = other;
	ent->touch = NULL;
	ent->think = HookBehavior;
	ent->nextthink = level.time + FRAMETIME;
}


void HookAirborne(edict_t *ent)
{
	vec3_t chainvec;		// chain's vector
	float chainlen;			// length of extended chain

	// get info about chain
	_VectorSubtract(ent->s.origin, ent->owner->s.origin, chainvec);
	chainlen = VectorLength(chainvec);

	if ((!(ent->owner->client->hookstate_ & HOOK_ON)) || (chainlen > hook_max_len->value))
	{
		DropHook(ent);
		return;
	}

	MaintainLinks(ent);

	ent->nextthink = level.time + FRAMETIME;
}


void FireHook(edict_t *ent)
{
	edict_t *newhook;
	vec3_t	offset, start;
	vec3_t	forward, right;
	int		damage;

	// determine the damage the hook will inflict
	damage = 10;
	if (ent->client->quad_framenum > level.framenum)
		damage *= 4;

	// derive point of hook origin
	AngleVectors(ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 8, 8, ent->viewheight - 8);
	P_ProjectSource_Reverse(ent->client, ent->s.origin, offset, forward, right, start);

	// spawn hook
	newhook = G_Spawn();
	VectorCopy(start, newhook->s.origin);
	VectorCopy(forward, newhook->movedir);
	vectoangles(forward, newhook->s.angles);
	VectorScale(forward, hook_speed->value, newhook->velocity);
	VectorSet(newhook->avelocity, 0, 0, -800);
	newhook->movetype = MOVETYPE_FLYMISSILE;
	newhook->clipmask = MASK_SHOT;
	newhook->solid = SOLID_BBOX;
	VectorClear(newhook->mins);
	VectorClear(newhook->maxs);
	newhook->s.modelindex = gi.modelindex("models/items/hook/tris.md2");
	newhook->owner = ent;
	newhook->dmg = damage;

	// keeps track of motor chain sound played 
	newhook->sounds = 0;

	// play hook launching sound
	gi.sound(ent, CHAN_HOOK, gi.soundindex("medic/medatck2.wav"), 1, ATTN_IDLE, 0);

	// specify actions to follow 
	newhook->touch = HookTouch;
	newhook->think = HookAirborne;
	newhook->nextthink = level.time + FRAMETIME;

	gi.linkentity(newhook);
}


void Cmd_Hook_f(edict_t *ent)
{
	char *s;
	int *hookstate_;

	// get the first hook argument
	s = gi.argv(1);

	// create intermediate value
	hookstate_ = &ent->client->hookstate_;

	if ((ent->solid != SOLID_NOT) && (ent->deadflag == DEAD_NO) &&
		(!(*hookstate_ & HOOK_ON)) && (Q_stricmp(s, "action") == 0))
	{
		// flags hook as being active 
		*hookstate_ = HOOK_ON;

		FireHook(ent);
		return;
	}

	if (*hookstate_ & HOOK_ON)
	{
		// release hook	
		if (Q_stricmp(s, "action") == 0)
		{
			*hookstate_ = 0;
			return;
		}

		// fixme:
		// hop of chain and release hook when the following conditions apply
		//		if ((ent->client->ps.pmove.pm_flags & PMF_JUMP_HELD) )		// jump is pressed
		//			(*hookstate_ & HOOK_IN) &&								// hook is attached
		//			(!(ent->client->ps.pmove.pm_flags & PMF_ON_GROUND)))	// player not on ground
		//				(!(self.flags & FL_INWATER))	)	// player not in water
		//		{
		//			ent->velocity[2] += 200;
		//			gi.sound (ent, CHAN_BODY, gi.soundindex ("medic/medatck2.wav"), 1, ATTN_IDLE, 0);
		//			*hookstate_ = 0;
		//			return;
		//		}

		// deactivate chain growth or shrink
		if (Q_stricmp(s, "stop") == 0)
		{
			*hookstate_ -= *hookstate_ & (GROW_ON | SHRINK_ON);
			return;
		}

		// activate chain growth
		if (Q_stricmp(s, "grow") == 0)
		{
			*hookstate_ |= GROW_ON;
			*hookstate_ -= *hookstate_ & SHRINK_ON;
			return;
		}

		// activate chain shrinking
		if (Q_stricmp(s, "shrink") == 0)
		{
			*hookstate_ |= SHRINK_ON;
			*hookstate_ -= *hookstate_ & GROW_ON;
		}
	}
}