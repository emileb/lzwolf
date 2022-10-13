/*
** actor.cpp
**
**---------------------------------------------------------------------------
** Copyright 2011 Braden Obrzut
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include <map>
#include <sstream>
#include <iostream>
#include "actor.h"
#include "a_inventory.h"
#include "farchive.h"
#include "gamemap.h"
#include "g_mapinfo.h"
#include "id_ca.h"
#include "id_sd.h"
#include "thinker.h"
#include "thingdef/thingdef.h"
#include "thingdef/thingdef_expression.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_loadsave.h"
#include "wl_state.h"
#include "wl_draw.h"
#include "id_us.h"
#include "m_random.h"

void T_ExplodeProjectile(AActor *self, AActor *target);
void T_Projectile(AActor *self);

// Old save compatibility
void AActorProxy::Serialize(FArchive &arc)
{
	Super::Serialize(arc);

	bool enabled;
	arc << enabled << actualObject;
}
IMPLEMENT_INTERNAL_CLASS(AActorProxy)

////////////////////////////////////////////////////////////////////////////////

Frame::~Frame()
{
	if(freeActionArgs)
	{
		// We need to delete CallArguments objects. Since we don't default to
		// NULL on these we need to check if a pointer has been set to know if
		// we created an object.
		if(action.pointer)
			delete action.args;
		if(thinker.pointer)
			delete thinker.args;
	}
}

static FRandom pr_statetics("StateTics");
int Frame::GetTics() const
{
	if(randDuration)
		return duration + pr_statetics.GenRand32() % (randDuration + 1);
	return duration;
}

bool Frame::ActionCall::operator() (AActor *self, AActor *stateOwner, const Frame * const caller, ActionResult *result) const
{
	if(pointer)
	{
		args->Evaluate(self);
		return pointer(self, stateOwner, caller, *args, result);
	}
	return false;
}

FArchive &operator<< (FArchive &arc, const Frame *&frame)
{
	if(arc.IsStoring())
	{
		// Find a class which held this state.
		// This should always be able to be found.
		const ClassDef *cls = NULL;
		if(frame)
		{
			ClassDef::ClassIterator iter = ClassDef::GetClassIterator();
			ClassDef::ClassPair *pair;
			while(iter.NextPair(pair))
			{
				cls = pair->Value;
				if(cls->IsStateOwner(frame))
					break;
			}

			arc << cls;
			arc << const_cast<Frame *>(frame)->index;
		}
		else
		{
			arc << cls;
		}
	}
	else
	{
		const ClassDef *cls;
		unsigned int frameIndex;

		arc << cls;
		if(cls)
		{
			arc << frameIndex;

			frame = cls->GetState(frameIndex);
		}
		else
			frame = NULL;
	}

	return arc;
}

////////////////////////////////////////////////////////////////////////////////

EmbeddedList<AActor>::List AActor::actors;
PointerIndexTable<ExpressionNode> AActor::damageExpressions;
PointerIndexTable<AActor::DropList> AActor::dropItems;
PointerIndexTable<AActor::DamageResistanceList> AActor::damageResistances;
PointerIndexTable<AActor::HaloLightList> AActor::haloLights;
PointerIndexTable<AActor::ZoneLightList> AActor::zoneLights;
PointerIndexTable<AActor::FilterposWrapList> AActor::filterposWraps;
PointerIndexTable<AActor::FilterposThrustList> AActor::filterposThrusts;
PointerIndexTable<AActor::FilterposWaveList> AActor::filterposWaves;
PointerIndexTable<AActor::EnemyFactionList> AActor::enemyFactions;
PointerIndexTable<AActor::InterrogateItemList> AActor::interrogateItems;
IMPLEMENT_POINTY_CLASS(Actor)
	DECLARE_POINTER(inventory)
	DECLARE_POINTER(target)
END_POINTERS

namespace ActorSpawnID
{
	unsigned int LastKey = 0;

	void NewActor (AActor *actor)
	{
		actor->spawnid = LastKey++;
	}

	void UnlinkActor (AActor *actor)
	{
		actor->spawnid = 0;
	}

	void Serialize(FArchive &arc)
	{
		arc << LastKey;
	}
}

void AActor::AddInventory(AInventory *item)
{
	// Check if it's already attached to an actor
	if (item->owner != NULL)
	{
		// Is it attached to us?
		if (item->owner == this)
			return;

		// No, then remove it from the other actor first
		item->owner->RemoveInventory (item);
	}

	item->AttachToOwner(this);
	item->inventory = inventory;
	inventory = item;

	GC::WriteBarrier(item);
}

void AActor::ClearCounters()
{
	if(flags & FL_COUNTITEM)
		--gamestate.treasuretotal;
	if((flags & FL_COUNTKILL) && health > 0)
		--gamestate.killtotal;
	if(flags & FL_COUNTSECRET)
		--gamestate.secrettotal;
	flags &= ~(FL_COUNTITEM|FL_COUNTKILL|FL_COUNTSECRET);
}

void AActor::ClearInventory()
{
	while(inventory)
		RemoveInventory(inventory);
}

void AActor::Destroy()
{
	Super::Destroy();
	RemoveFromWorld();
	ActorSpawnID::UnlinkActor (this);

	// Inventory items don't have a registered thinker so we must free them now
	if(inventory)
	{
		inventory->Destroy();
		inventory = NULL;
	}
}

static FRandom pr_dropitem("DropItem");
void AActor::Die()
{
	if(target && target->player)
		target->player->GivePoints(points);
	else if(points)
	{
		// The targetting system may need some refinement, so if we don't have
		// a usable target to give points to then we should give to player 1
		// and possibly investigate.
		players[0].GivePoints(points);
		NetDPrintf("%s %d points with no target\n", __FUNCTION__, points);
	}

	// obituary
	if(target)
	{
		ObituaryMessage(target);
	}

	if(flags & FL_COUNTKILL)
	{
		gamestate.killcount++;

		if(this->spawnThingNum.first)
		{
			gamestate.phubworld->setThingKilled(gamestate.mapname,
			                                    this->spawnThingNum.second,
												HubWorld::CountedType::KILLS);
		}
	}
	flags &= ~FL_SHOOTABLE;

	if(flags & FL_MISSILE)
	{
		T_ExplodeProjectile(this, NULL);
		return;
	}

	DropList *dropitems = GetDropList();
	if(dropitems)
	{
		DropList::Iterator item = dropitems->Head();
		DropList::Iterator bestDrop = NULL; // For FL_DROPBASEDONTARGET
		do
		{
			DropList::Iterator drop = item;
			if(pr_dropitem() <= drop->probability)
			{
				const ClassDef *cls = ClassDef::FindClass(drop->className);
				if(cls)
				{
					if(flags & FL_DROPBASEDONTARGET)
					{
						AInventory *inv = target ? target->FindInventory(cls->GetReplacement()) : NULL;
						if(!inv || !bestDrop)
							bestDrop = drop;

						if(item.HasNext())
							continue;
						else
						{
							cls = ClassDef::FindClass(bestDrop->className);
							drop = bestDrop;
						}
					}

					// We can't use tilex/tiley since it's used primiarily by
					// the AI, so it can be off by one.
					static const fixed TILEMASK = ~(gameinfo.DropItemTileSize-1);

					AActor * const actor = AActor::Spawn(cls, (x&TILEMASK)+(gameinfo.DropItemTileSize/2), (y&TILEMASK)+(gameinfo.DropItemTileSize/2), 0, SPAWN_AllowReplacement);
					actor->angle = angle;
					actor->dir = dir;
					actor->trydir = nodir;

					if(cls->IsDescendantOf(NATIVE_CLASS(Inventory)))
					{
						AInventory * const inv = static_cast<AInventory *>(actor);

						// Ammo is multiplied by 0.5
						if(drop->amount > 0)
						{
							// TODO: When a dropammofactor is specified it should
							// apply here too.
							inv->amount = drop->amount;
						}
						else if(cls->IsDescendantOf(NATIVE_CLASS(Ammo)) && inv->amount > 1)
							inv->amount /= 2;

						// TODO: In ZDoom dropped weapons have their ammo multiplied as well
						// but in Wolf3D weapons always have 6 bullets.
					}
				}
			}
		}
		while(item.Next());
	}

	bool isExtremelyDead = health < -GetClass()->Meta.GetMetaInt(AMETA_GibHealth, (GetDefault()->health*gameinfo.GibFactor)>>FRACBITS);
	if (killerdamagetype && isExtremelyDead)
	{
		ADamage *damage = static_cast<ADamage *>(players[0].mo->FindInventory(killerdamagetype));
		if (damage && damage->noxdeath)
			isExtremelyDead = false;
	}

	const Frame *deathstate = NULL;
	if (!deathstate && isExtremelyDead && killerdamagetype)
	{
		std::stringstream ss;
		ss << "XDeath_" << std::string(killerdamagetype->GetName().GetChars());
		deathstate = FindState(ss.str().c_str());
	}
	if(!deathstate && isExtremelyDead)
		deathstate = FindState(NAME_XDeath);

	if (!deathstate && killerdamagetype)
	{
		std::stringstream ss;
		ss << "Death_" << std::string(killerdamagetype->GetName().GetChars());
		deathstate = FindState(ss.str().c_str());
	}
	if(!deathstate)
		deathstate = FindState(NAME_Death);

	if(deathstate)
		SetState(deathstate);
	else
		Destroy();
}

void AActor::EnterZone(const MapZone *zone)
{
	if(zone)
		soundZone = zone;
}

AInventory *AActor::FindInventory(const ClassDef *cls)
{
	if(inventory == NULL)
		return NULL;

	AInventory *check = inventory;
	do
	{
		if(check->IsA(cls))
			return check;
	}
	while((check = check->inventory));
	return NULL;
}

const Frame *AActor::FindState(const FName &name) const
{
	return GetClass()->FindState(name);
}

int AActor::GetDamage()
{
	int expression = GetClass()->Meta.GetMetaInt(AMETA_Damage, -1);
	if(expression >= 0)
		return static_cast<int>(damageExpressions[expression]->Evaluate(this).GetInt());
	return 0;
}

AActor::DropList *AActor::GetDropList() const
{
	int dropitemsIndex = GetClass()->Meta.GetMetaInt(AMETA_DropItems, -1);
	if(dropitemsIndex == -1)
		return NULL;
	return dropItems[dropitemsIndex];
}

AActor::DamageResistanceList *AActor::GetDamageResistanceList() const
{
	int damageresistancesIndex = GetClass()->Meta.GetMetaInt(AMETA_DamageResistances, -1);
	if(damageresistancesIndex == -1)
		return NULL;
	return damageResistances[damageresistancesIndex];
}

AActor::HaloLightList *AActor::GetHaloLightList() const
{
	int halolightsIndex = GetClass()->Meta.GetMetaInt(AMETA_HaloLights, -1);
	if(halolightsIndex == -1)
		return NULL;
	return haloLights[halolightsIndex];
}

AActor::ZoneLightList *AActor::GetZoneLightList() const
{
	int zonelightsIndex = GetClass()->Meta.GetMetaInt(AMETA_ZoneLights, -1);
	if(zonelightsIndex == -1)
		return NULL;
	return zoneLights[zonelightsIndex];
}

AActor::FilterposWrapList *AActor::GetFilterposWrapList() const
{
	int filterposwrapsIndex = GetClass()->Meta.GetMetaInt(AMETA_FilterposWraps, -1);
	if(filterposwrapsIndex == -1)
		return NULL;
	return filterposWraps[filterposwrapsIndex];
}

AActor::FilterposThrustList *AActor::GetFilterposThrustList() const
{
	int filterposthrustsIndex = GetClass()->Meta.GetMetaInt(AMETA_FilterposThrusts, -1);
	if(filterposthrustsIndex == -1)
		return NULL;
	return filterposThrusts[filterposthrustsIndex];
}

AActor::FilterposWaveList *AActor::GetFilterposWaveList() const
{
	int filterposwavesIndex = GetClass()->Meta.GetMetaInt(AMETA_FilterposWaves, -1);
	if(filterposwavesIndex == -1)
		return NULL;
	return filterposWaves[filterposwavesIndex];
}

AActor::EnemyFactionList *AActor::GetEnemyFactionList() const
{
	int enemyfactionsIndex = GetClass()->Meta.GetMetaInt(AMETA_EnemyFactions, -1);
	if(enemyfactionsIndex == -1)
		return NULL;
	return enemyFactions[enemyfactionsIndex];
}

AActor::InterrogateItemList *AActor::GetInterrogateItemList() const
{
	int interrogateitemsIndex = GetClass()->Meta.GetMetaInt(AMETA_InterrogateItems, -1);
	if(interrogateitemsIndex == -1)
		return NULL;
	return interrogateItems[interrogateitemsIndex];
}

const AActor *AActor::GetDefault() const
{
	return GetClass()->GetDefault();
}

bool AActor::GiveInventory(const ClassDef *cls, int amount, bool allowreplacement)
{
	AInventory *inv = (AInventory *) AActor::Spawn(cls, 0, 0, 0, allowreplacement ? SPAWN_AllowReplacement : 0);

	if(amount)
	{
		if(inv->IsKindOf(NATIVE_CLASS(Health)))
			inv->amount *= amount;
		else
			inv->amount = amount;
	}

	PlaySoundLocActor(inv->pickupsound, this);

	inv->ClearCounters();
	inv->RemoveFromWorld();
	if(!inv->CallTryPickup(this))
	{
		inv->Destroy();
		return false;
	}
	return true;
}

void AActor::Init()
{
	Super::Init();

	ObjectFlags |= OF_JustSpawned;

	distance = 0;
	dir = nodir;
	trydir = nodir;
	soundZone = NULL;
	inventory = NULL;

	actors.Push(this);
	if(!loadedgame)
		Thinker::Activate();

	if(SpawnState)
		SetState(SpawnState, true);
	else
	{
		state = NULL;
		Destroy();
	}
}

// Approximate if a state sequence is running by checking if we are in a
// contiguous sequence.
bool AActor::InStateSequence(const Frame *basestate) const
{
	if(!basestate)
		return false;

	while(state != basestate)
	{
		if(basestate->next != basestate+1)
			return false;
		++basestate;
	}
	return true;
}

bool AActor::IsFast() const
{
	return (flags & FL_ALWAYSFAST) || gamestate.difficulty->FastMonsters;
}

void AActor::PrintInventory()
{
	Printf("%s inventory:\n", GetClass()->GetName().GetChars());
	AInventory *item = inventory;
	while(item)
	{
		Printf("  %s (%d/%d)\n", item->GetClass()->GetName().GetChars(), item->amount, item->maxamount);
		item = item->inventory;
	}
}

FArchive &operator<< (FArchive &arc, AActor::FilterposWaveLastMove &lastMove)
{
	arc << lastMove.id << lastMove.delta;
	return arc;
}

void AActor::Serialize(FArchive &arc)
{
	bool hasActorRef = actors.IsLinked(this);

	if(arc.IsStoring())
		arc.WriteSprite(sprite);
	else
		sprite = arc.ReadSprite();

	BYTE dir = this->dir;
	arc << dir;
	this->dir = static_cast<dirtype>(dir);

	BYTE trydir = this->trydir;
	arc << trydir;
	this->trydir = static_cast<dirtype>(trydir);

	arc << spawnid
		<< flags
		<< extraflags
		<< distance
		<< x
		<< y;
	if(GameSave::SaveProdVersion >= 0x001003FF && GameSave::SaveVersion >= 1507591295)
		arc << z;
	arc << velx
		<< vely
		<< angle
		<< pitch
		<< health
		<< speed
		<< runspeed
		<< points
		<< radius
		<< ticcount
		<< state
		<< viewx
		<< viewheight
		<< transx
		<< transy
		<< zoneindex;
	if(GameSave::SaveVersion >= 1393719642)
		arc << overheadIcon;
	arc << sighttime
		<< sightrandom
		<< minmissilechance
		<< painchance
		<< missilefrequency
		<< movecount
		<< meleerange
		<< activesound
		<< attacksound
		<< deathsound
		<< seesound
		<< painsound
		<< temp1
		<< hidden
		<< player
		<< inventory
		<< soundZone
		<< spawnThingNum
		<< activationtype;
	if(GameSave::SaveProdVersion >= 0x001003FF && GameSave::SaveVersion >= 1459043051)
		arc << target;
	if(arc.IsLoading() && (GameSave::SaveProdVersion < 0x001002FF || GameSave::SaveVersion < 1382102747))
	{
		TObjPtr<AActorProxy> proxy;
		arc << proxy;
	}
	arc << hasActorRef;
	arc << haloLightMask;
	arc << zoneLightMask;
	arc << litfilter;
	arc << singlespawn;
	arc << interrogateItemsUsed;
	arc << informant.ammo;
	arc << informant.s_tilex;
	arc << informant.s_tiley;
	arc << DamageFactor;
	arc << filterposwaveLastMoves;

	if(GameSave::SaveProdVersion >= 0x001002FF && GameSave::SaveVersion > 1374914454)
		arc << projectilepassheight;

	arc << missileParent;
	arc << PatrolFilterKey;
	arc << PendingPatrolChange;
	arc << PendingPatrolAngle;
	dir = this->PendingPatrolDir;
	arc << dir;
	this->PendingPatrolDir = static_cast<dirtype>(dir);

	arc << UseTriggerFilterKey;
	arc << FlipSprite;
	arc << TwoSidedRotate;

	if(arc.IsLoading() && !hasActorRef)
		actors.Remove(this);

	Super::Serialize(arc);
}

void AActor::SetState(const Frame *state, bool norun)
{
	if(state == NULL)
	{
		Destroy();
		return;
	}

	this->state = state;
	sprite = state->spriteInf;
	ticcount = state->GetTics();
	if(!norun)
	{
		state->action(this, this, state);

		while(ticcount == 0)
		{
			this->state = this->state->next;
			if(!this->state)
			{
				Destroy();
				break;
			}
			else
			{
				sprite = this->state->spriteInf;
				ticcount = this->state->GetTics();
				this->state->action(this, this, this->state);
			}
		}
	}
}

void AActor::SpawnFog()
{
	if(const ClassDef *cls = ClassDef::FindClass("TeleportFog"))
	{
		AActor *fog = Spawn(cls, x, y, 0, SPAWN_AllowReplacement);
		fog->angle = angle;
		fog->target = this;
	}
}

bool AActor::Teleport(fixed x, fixed y, angle_t angle, bool nofog)
{
	const MapSpot destination = map->GetSpot(x>>FRACBITS, y>>FRACBITS, 0);

	// For non-players, only teleport if spot is clear.
	if(!player)
	{
		if(!TrySpot(this, destination))
			return false;
	}

	if(!nofog)
		SpawnFog(); // Source fog

	this->x = x;
	this->y = y;
	this->angle = angle;

	EnterZone(destination->zone);

	if(!nofog)
		SpawnFog(); // Destination fog
	return true;
}

void AActor::ApplyFilterpos (FilterposWrap wrap)
{
	const double delta = wrap.x2 - wrap.x1;
	if (wrap.axis >= 3 || delta <= 0)
		Quit ("FilterposWrap has invalid parameters!");

	double v[3];
	v[0] = FIXED2FLOAT(x);
	v[1] = FIXED2FLOAT(y);
	v[2] = FIXED2FLOAT(z);

	double &val = v[wrap.axis];
	if (val < wrap.x1)
		val += delta;
	if (val > wrap.x2)
		val -= delta;

	x = FLOAT2FIXED(v[0]);
	y = FLOAT2FIXED(v[1]);
	z = FLOAT2FIXED(v[2]);
}

fixed &AActor::GetCoordRef (unsigned int axis)
{
	if (axis >= 3)
		Quit ("Invalid axis!");
	return (axis==0 ? x : (axis==1 ? y : z));
}

void AActor::ApplyFilterpos (FilterposThrust thrust)
{
	fixed move = 0;
	switch (thrust.src)
	{
	case FilterposThrustSource::forwardThrust:
		move = players[0].mo->forwardthrust;
		break;
	case FilterposThrustSource::sideThrust:
		move = players[0].mo->sidethrust;
		break;
	case FilterposThrustSource::rotation:
		move = players[0].mo->rotthrust * -50;
		break;
	}
	GetCoordRef (thrust.axis) -= move;
}

fixed &AActor::GetFilterposWaveOldDelta (int id)
{
	unsigned int i;
	for (i = 0; i < filterposwaveLastMoves.Size(); i++)
	{
		FilterposWaveLastMove &lm = filterposwaveLastMoves[i];
		if (lm.id == id)
			return lm.delta;
	}

	FilterposWaveLastMove lm;
	lm.id = id;
	lm.delta = 0;
	return filterposwaveLastMoves[filterposwaveLastMoves.Push(lm)].delta;
}

void AActor::ApplyFilterpos (FilterposWave wave)
{
	const uint32_t durTicks = (uint32_t)(wave.period * 1000);
	if (durTicks <= 0)
		Quit ("Invalid duration!");
	const uint32_t currentTick = (SDL_GetTicks() % durTicks);
	const uint32_t curFineangle = currentTick*FINEANGLES/durTicks;
	const fixed delta = FLOAT2FIXED(wave.amplitude *
		FIXED2FLOAT((wave.usesine ? finesine : finecosine)[curFineangle]));
	fixed &olddelta = GetFilterposWaveOldDelta (wave.id);
	GetCoordRef (wave.axis) += delta - olddelta;
	olddelta = delta;
}

namespace FilterposApplier
{
	class Base
	{
	public:
		virtual ~Base() { }

		virtual void Execute (AActor *actor) = 0;
	};

	template <typename T>
	class Filter : public Base
	{
		T v;

	public:
		explicit Filter(T v_) : v(v_)
		{
		}

		virtual void Execute (AActor *actor)
		{
			actor->ApplyFilterpos (v);
		}
	};

	template <typename T>
	TSharedPtr<Base> MakeFilter (T v)
	{
		return new Filter<T>(v);
	}
	
	typedef std::map<int, TSharedPtr<Base> > ExecMap;

	void InitExecMap (AActor *actor, ExecMap &m)
	{
		{
			typedef AActor::FilterposWrapList Li;

			Li *li = actor->GetFilterposWrapList();
			if (li)
			{
				Li::Iterator item = li->Head();
				do
				{
					Li::Iterator filterposWrap = item;
					m[filterposWrap->id] = MakeFilter (*filterposWrap);
				}
				while(item.Next());
			}
		}

		{
			typedef AActor::FilterposThrustList Li;

			Li *li = actor->GetFilterposThrustList();
			if (li)
			{
				Li::Iterator item = li->Head();
				do
				{
					Li::Iterator filterposThrust = item;
					m[filterposThrust->id] = MakeFilter (*filterposThrust);
				}
				while(item.Next());
			}
		}

		{
			typedef AActor::FilterposWaveList Li;

			Li *li = actor->GetFilterposWaveList();
			if (li)
			{
				Li::Iterator item = li->Head();
				do
				{
					Li::Iterator filterposWave = item;
					m[filterposWave->id] = MakeFilter (*filterposWave);
				}
				while(item.Next());
			}
		}
	}

	void Execute (AActor *actor)
	{
		ExecMap m;
		InitExecMap (actor, m);

		int id;
		for (id = 0; m.find(id) != m.end(); ++id)
			m.find(id)->second->Execute (actor);
	}
}

void AActor::Activate (AActor *activator)
{
}

void AActor::Deactivate (AActor *activator)
{
}

void AActor::Tick()
{
	// If we just spawned we're not ready to be ticked yet
	// Otherwise we might tick on the same tick we're spawned which would cause
	// an actor with a duration of 1 tic to never display
	if(ObjectFlags & OF_JustSpawned)
	{
		ObjectFlags &= ~OF_JustSpawned;
		return;
	}

	if(state == NULL)
	{
		Destroy();
		return;
	}

	if(ticcount > 0)
		--ticcount;

	if(ticcount == 0)
	{
		SetState(state->next);
		if(ObjectFlags & OF_EuthanizeMe)
			return;
	}

	state->thinker(this, this, state);

	if(flags & FL_MISSILE)
		T_Projectile(this);
	
	FilterposApplier::Execute (this);
}

// Remove an actor from the game world without destroying it.  This will allow
// us to transfer items into inventory for example.
void AActor::RemoveFromWorld()
{
	actors.Remove(this);
	if(IsThinking())
		Thinker::Deactivate();
	LoopedAudio::stopSoundFrom (this->spawnid);
}

void AActor::RemoveInventory(AInventory *item)
{
	AInventory *inv, **invp;

	if (item != NULL && item->owner != NULL)	// can happen if the owner was destroyed by some action from an item's use state.
	{
		invp = &item->owner->inventory;
		for (inv = *invp; inv != NULL; invp = &inv->inventory, inv = *invp)
		{
			if (inv == item)
			{
				*invp = item->inventory;
				item->DetachFromOwner();
				item->owner = NULL;
				item->inventory = NULL;
				break;
			}
		}
	}
}

/* When we spawn an actor we add them to this list. After the tic has finished
 * processing we process this list to handle any start up actions.
 *
 * This is done so that we don't duplicate tics and actors appear on screen
 * when they should. We can't do this in Spawn() since we want certain
 * properties of the actor (velocity) to be setup before calling actions.
 */
static TArray<AActor *> SpawnedActors;
void AActor::FinishSpawningActors()
{
	unsigned int i = SpawnedActors.Size();
	while(i-- > 0)
	{
		AActor * const actor = SpawnedActors[i];

		// Run the first action pointer and all zero tic states!
		actor->SetState(actor->state);
		actor->ObjectFlags &= ~OF_JustSpawned;
	}
	SpawnedActors.Clear();
}

FRandom pr_spawnmobj("SpawnActor");
AActor *AActor::Spawn(const ClassDef *type, fixed x, fixed y, fixed z, int flags)
{
	if(type == NULL)
	{
		printf("Tried to spawn classless actor.\n");
		return NULL;
	}

	if(flags & SPAWN_AllowReplacement)
	{
		if(type->GetReplacementPrb() == 0 || pr_spawnmobj() < type->GetReplacementPrb())
		{
			type = type->GetReplacement();
		}
	}

	if (type->GetDefault()->singlespawn)
	{
		for(AActor::Iterator iter = AActor::GetIterator();iter.Next();)
		{
			AActor * const other = iter;
			if (other->GetClass() == type)
				return NULL;
		}
	}

	AActor *actor = type->CreateInstance();
	actor->x = x;
	actor->y = y;
	actor->z = z;
	actor->velx = 0;
	actor->vely = 0;
	actor->health = actor->SpawnHealth();
	actor->informant.ammo = 0;
	actor->informant.s_tilex = 0xff;
	actor->informant.s_tiley = 0xff;

	MapSpot spot = map->GetSpot(actor->tilex, actor->tiley, 0);
	actor->EnterZone(spot->zone);

	// Execute begin play hook and then check if the actor is still alive.
	actor->BeginPlay();
	if(actor->ObjectFlags & OF_EuthanizeMe)
		return NULL;

	if(actor->flags & FL_COUNTKILL)
		++gamestate.killtotal;
	if(actor->flags & FL_COUNTITEM)
		++gamestate.treasuretotal;
	if(actor->flags & FL_COUNTSECRET)
		++gamestate.secrettotal;

	if(levelInfo && levelInfo->SecretDeathSounds)
	{
		const char* snd = type->Meta.GetMetaString(AMETA_SecretDeathSound);
		if(snd)
			actor->deathsound = snd;
	}

	if(actor->flags & FL_MISSILE)
	{
		PlaySoundLocActor(actor->seesound, actor);
		if((actor->flags & FL_RANDOMIZE) && actor->ticcount > 0)
		{
			actor->ticcount -= pr_spawnmobj() & 7;
			if(actor->ticcount < 1)
				actor->ticcount = 1;
		}
	}
	else
	{
		if((actor->flags & FL_RANDOMIZE) && actor->ticcount > 0)
			actor->ticcount = pr_spawnmobj() % actor->ticcount;
	}

	// Change between patrolling and normal spawn and also execute any zero
	// tic functions.
	if(flags & SPAWN_Patrol)
	{
		actor->flags |= FL_PATHING;

		// Pathing monsters should take at least a one tile step.
		// Otherwise the paths will break early.
		actor->distance = TILEGLOBAL;
		if(actor->PathState)
		{
			actor->SetState(actor->PathState, true);
			if(actor->flags & FL_RANDOMIZE)
				actor->ticcount = pr_spawnmobj() % actor->ticcount;
		}
	}

	SpawnedActors.Push(actor);
	ActorSpawnID::NewActor (actor);
	return actor;
}

int32_t AActor::SpawnHealth() const
{
	return GetClass()->Meta.GetMetaInt(AMETA_DefaultHealth1 + gamestate.difficulty->SpawnFilter, health);
}

const char *AActor::InfoMessage ()
{
	return GetClass()->Meta.GetMetaString (AMETA_InfoMessage);
}

DEFINE_SYMBOL(Actor, angle)
DEFINE_SYMBOL(Actor, health)
DEFINE_SYMBOL(Actor, loaded)
DEFINE_SYMBOL(Actor, zoneindex)

//==============================================================================

/*
===================
=
= Player travel functions
=
===================
*/

void StartTravel ()
{
	// Set thinker priorities to TRAVEL so that they don't get wiped on level
	// load.  We'll transfer them to a new actor.

	AActor *player = players[0].mo;

	player->SetPriority(ThinkerList::TRAVEL);
}

void FinishTravel ()
{
	gamestate.victoryflag = false;

	ThinkerList::Iterator node = thinkerList->GetHead(ThinkerList::TRAVEL);
	if(!node)
		return;

	do
	{
		AActor *actor = static_cast<AActor *>((Thinker*)node);
		if(actor->IsKindOf(NATIVE_CLASS(PlayerPawn)))
		{
			APlayerPawn *player = static_cast<APlayerPawn *>(actor);
			if(player->player == &players[0])
			{
				AActor *playertmp = players[0].mo;
				player->x = playertmp->x;
				player->y = playertmp->y;
				player->angle = playertmp->angle;
				player->EnterZone(playertmp->GetZone());

				players[0].mo = player;
				players[0].camera = player;
				playertmp->Destroy();

				// We must move the linked list iterator here since we'll
				// transfer to the new linked list at the SetPriority call
				player->SetPriority(ThinkerList::PLAYER);
				continue;
			}
		}
	}
	while(node.Next());
}

////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_CLASS(Lit)

const ClassDef *ALit::GetLitType() const
{
	const ClassDef *cls = GetClass();
	while(cls->GetParent() != NATIVE_CLASS(Lit))
		cls = cls->GetParent();
	return cls;
}

////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_CLASS(Collateral)

const ClassDef *ACollateral::GetCollateralType() const
{
	const ClassDef *cls = GetClass();
	while(cls->GetParent() != NATIVE_CLASS(Collateral))
		cls = cls->GetParent();
	return cls;
}
