#include <map>
#include <vector>
#include <cmath>
#include <climits>
#include <cctype>

#include "wl_def.h"
#include "wl_agent.h"
#include "wl_game.h"
#include "wl_play.h"
#include "textures/textures.h"
#include "id_ca.h"
#include "id_us.h"
#include "id_vh.h"
#include "scanner.h"
#include "w_wad.h"
#include "m_random.h"
#include "thingdef/thingdef.h"
#include "g_mapinfo.h"
#include "a_inventory.h"
#include "a_keys.h"
#include "wl_iwad.h"

EXTERN_CVAR (Int, godmode)

/*
=============================================================================

							STATUS WINDOW STUFF

=============================================================================
*/

#define STATUSLINES     40

struct LatchConfig
{
	unsigned int Enabled;
	unsigned int Digits;
	unsigned int X;
	unsigned int Y;
};
struct InventoryLatchConfig : public LatchConfig
{
	FString ClassName;
	FString FoundTexture;
	FString MissingTexture;
};
static struct StatusBarConfig_t
{
	LatchConfig Floor, Score, Lives, Health, Ammo, ArmorPoints;
	LatchConfig Items;

	// The following don't use the digits
	LatchConfig Mugshot, Keys, Weapon, Armor;

	std::vector<InventoryLatchConfig> Inventory;

	bool ShowWithoutKey;
} StatusBarConfig = {
	{1, 2, 16, 16},      // Floor
	{1, 6, 48, 16},      // Score
	{1, 1, 112, 16},     // Lives
	{1, 3, 168, 16},     // Health
	{1, 3, 208, 16},     // Ammo
	{0, 3, 280, 16},     // ArmorPoints
	{0, 2, 280, 16},     // Items
	{1, 0, 136, 4},      // Mugshot
	{1, 0, 240, 4},      // Keys
	{1, 0, 256, 8},      // Weapon
	{1, 0, 256, 8}       // Armor
};

class WolfStatusBar : public DBaseStatusBar
{
public:
	WolfStatusBar() : facecount(0), mac(false)
	{
		if(IWad::CheckGameFilter("Noah"))
		{
			// Change default configuration
			StatusBarConfig.Floor.X = 16;
			StatusBarConfig.Floor.Digits = 3;
			StatusBarConfig.Score.X = 64;
			StatusBarConfig.Lives.X = 128;
			StatusBarConfig.Health.X = 184;
			StatusBarConfig.Ammo.X = 224;
			StatusBarConfig.Items.Enabled = true;
			StatusBarConfig.Mugshot.X = 152;
			StatusBarConfig.Keys.X = 256;
			StatusBarConfig.Weapon.Enabled = false;
		}
		else if(IWad::CheckGameFilter("MacWolf3D"))
		{
			mac = true;
			StatusBarConfig.Floor.X = 8;
			StatusBarConfig.Floor.Digits = 4;
			StatusBarConfig.Score.Digits = 7;
			StatusBarConfig.Score.X = 56;
			StatusBarConfig.Lives.X = 188;
			StatusBarConfig.Health.X = 210;
			StatusBarConfig.Ammo.Digits = 3;
			StatusBarConfig.Ammo.X = 268;
			StatusBarConfig.Items.X = 128;
			StatusBarConfig.Items.Enabled = true;
			StatusBarConfig.Mugshot.X = 160;
			StatusBarConfig.Keys.X = 310;
			StatusBarConfig.Weapon.Enabled = false;
		}

		SetupStatusbar();
	}

	void DrawStatusBar();
	unsigned int GetHeight(bool top) { return top ? 0 : STATUSLINES+!mac; }
	void NewGame() { facecount = 0; }
	void RefreshBackground(bool noborder);
	void UpdateFace(int damage=0);
	void WeaponGrin();
	bool ShowWithoutKey() const;

private:
	static void LatchNumber (int x, int y, unsigned width, int32_t number, bool zerofill, bool cap=false);
	static void LatchString (int x, int y, unsigned width, const FString &str);
	static void StatusDrawFace(FTexture *pic);
	static void StatusDrawPic(unsigned x, unsigned y, const char* pic);

	FTextureID GetArmorIcon (int &points);

	void DrawAmmo();
	void DrawFace();
	void DrawLevel();
	void DrawLives();
	void DrawHealth();
	void DrawItems();
	void DrawKeys();
	void DrawInventory();
	void DrawScore();
	void DrawWeapon();
	void DrawArmor();
	void DrawArmorPoints();
	void SetupStatusbar();

	int facecount;
	bool mac;
};

DBaseStatusBar *CreateStatusBar_Wolf3D() { return new WolfStatusBar(); }

/*
==================
=
= StatusDrawPic
=
==================
*/

void WolfStatusBar::StatusDrawPic (unsigned x, unsigned y, const char* pic)
{
	VWB_DrawGraphic(TexMan(pic), x, 200-(STATUSLINES-y));
}

void WolfStatusBar::StatusDrawFace(FTexture *pic)
{
	VWB_DrawGraphic(pic, StatusBarConfig.Mugshot.X, 200-(STATUSLINES-StatusBarConfig.Mugshot.Y));
}


/*
==================
=
= DrawFace
=
==================
*/

void WolfStatusBar::DrawFace (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.Mugshot.Enabled) return;

	if(!gamestate.faceframe.isValid())
	{
		facecount = 0;
		UpdateFace();
	}

	if (players[ConsolePlayer].health)
		StatusDrawFace(TexMan(gamestate.faceframe));
	else
	{
		// TODO: Make this work based on damage types.
		// It gets uglier now that we can blame the source of a projectile we
		// have to check the class that fired it which is just wrong. One of
		// these days I'll get damage types in!
		static const ClassDef *schabbs = ClassDef::FindClass("Schabbs");
		if (players[ConsolePlayer].killerobj && players[ConsolePlayer].killerobj->IsKindOf(schabbs))
			StatusDrawFace(TexMan("STFMUT0"));
		else
			StatusDrawFace(TexMan("STFDEAD0"));
	}
}

/*
===============
=
= UpdateFace
=
= Calls draw face if time to change
=
===============
*/

void WolfStatusBar::WeaponGrin ()
{
	static FTextureID grin = TexMan.CheckForTexture("STFEVL0", FTexture::TEX_Any);
	gamestate.faceframe = grin;
	facecount = 140;
}

void WolfStatusBar::UpdateFace (int damage)
{
	static int oldDamageLevel = 0;
	static bool noGodFace = false;
	static FTextureID godmodeFace[3] = { TexMan.CheckForTexture("STFGOD0", FTexture::TEX_Any), TexMan.CheckForTexture("STFGOD1", FTexture::TEX_Any), TexMan.CheckForTexture("STFGOD2", FTexture::TEX_Any) };
	static FTextureID waitFace[2] = { TexMan.CheckForTexture("STFWAIT0", FTexture::TEX_Any), TexMan.CheckForTexture("STFWAIT1", FTexture::TEX_Any) };
	static FTextureID animations[7][3] =
	{
		{ TexMan.CheckForTexture("STFST00", FTexture::TEX_Any), TexMan.CheckForTexture("STFST01", FTexture::TEX_Any), TexMan.CheckForTexture("STFST02", FTexture::TEX_Any) },
		{ TexMan.CheckForTexture("STFST10", FTexture::TEX_Any), TexMan.CheckForTexture("STFST11", FTexture::TEX_Any), TexMan.CheckForTexture("STFST12", FTexture::TEX_Any) },
		{ TexMan.CheckForTexture("STFST20", FTexture::TEX_Any), TexMan.CheckForTexture("STFST21", FTexture::TEX_Any), TexMan.CheckForTexture("STFST22", FTexture::TEX_Any) },
		{ TexMan.CheckForTexture("STFST30", FTexture::TEX_Any), TexMan.CheckForTexture("STFST31", FTexture::TEX_Any), TexMan.CheckForTexture("STFST32", FTexture::TEX_Any) },
		{ TexMan.CheckForTexture("STFST40", FTexture::TEX_Any), TexMan.CheckForTexture("STFST41", FTexture::TEX_Any), TexMan.CheckForTexture("STFST42", FTexture::TEX_Any) },
		{ TexMan.CheckForTexture("STFST50", FTexture::TEX_Any), TexMan.CheckForTexture("STFST51", FTexture::TEX_Any), TexMan.CheckForTexture("STFST52", FTexture::TEX_Any) },
		{ TexMan.CheckForTexture("STFST60", FTexture::TEX_Any), TexMan.CheckForTexture("STFST61", FTexture::TEX_Any), TexMan.CheckForTexture("STFST62", FTexture::TEX_Any) },
	};
	static unsigned int faceAmimSet = animations[0][2].isValid() ? 3 : 2;
	static bool macDamage = !animations[2][0].isValid();

	const int maxHealth = players[ConsolePlayer].mo ? players[ConsolePlayer].mo->maxhealth : 100;
	const int damageLevel = macDamage ? (players[ConsolePlayer].health > (maxHealth>>2) ? 0 : 1)
		: MIN(6, players[ConsolePlayer].health > maxHealth ? 0 : (maxHealth-players[ConsolePlayer].health)/(maxHealth/6));
	if(damage)
	{
		static FTextureID ouchFace = TexMan.CheckForTexture("STFOUCH0", FTexture::TEX_Any);
		if(ouchFace.isValid() && damage > 30 && players[ConsolePlayer].health != 0)
		{
			gamestate.faceframe = ouchFace;
			facecount = 17;
		}
		else
		{
			// Update the face only if we've changed damage levels.
			if(damageLevel == oldDamageLevel)
				return;
			facecount = 0;
		}
	}
	oldDamageLevel = damageLevel;

	// OK Wolf apparently did something more along the lines of ++facecount > M_Random()
	// This doesn't seem to work as well with the new random generator, so lets take a different approach.
	if (--facecount <= 0)
	{
		facecount = ((M_Random()>>3)|0xF);

		if (funnyticount > 301 * 70)
		{
			funnyticount = 0;
			FTextureID pickedID = waitFace[M_Random() & 1];
			if(pickedID.isValid())
			{
				gamestate.faceframe = pickedID;
				facecount = 17;
				return;
			}
		}

		unsigned int facePick = M_Random()%faceAmimSet;

		if(godmode && !noGodFace)
		{
			gamestate.faceframe = godmodeFace[facePick];

			if(!gamestate.faceframe.isValid())
			{
				if(!godmodeFace[0].isValid())
					noGodFace = true;
				godmodeFace[1] = godmodeFace[2] = godmodeFace[0];
			}
			else
				return;
		}

		if(players[ConsolePlayer].mo)
			gamestate.faceframe = animations[damageLevel][facePick];
		else
			gamestate.faceframe = animations[0][0];
	}
}

/*
===============
=
= ShowWithoutKey
=
= Indicates status on whether key is required to show status bar while viewsize
= is at its maximum.
=
===============
*/

bool WolfStatusBar::ShowWithoutKey() const
{
	return StatusBarConfig.ShowWithoutKey;
}

/*
===============
=
= LatchNumber
=
= right justifies and pads with blanks
=
===============
*/

void WolfStatusBar::LatchNumber (int x, int y, unsigned width, int32_t number, bool zerofill, bool cap)
{
	FString str;
	if(zerofill)
		str.Format("%0*d", width, number);
	else
		str.Format("%*d", width, number);
	if(str.Len() > width && cap)
	{
		int maxval = width <= 9 ? (int) ceil(pow(10., (int)width))-1 : INT_MAX;
		str.Format("%d", maxval);
	}

	LatchString(x, y, width, str);
}

void WolfStatusBar::LatchString (int x, int y, unsigned width, const FString &str)
{
	static FFont *HudFont = NULL;
	if(!HudFont)
	{
		HudFont = V_GetFont("HudFont");
	}

	y = 200-(STATUSLINES-y);// + HudFont->GetHeight();

	int cwidth;
	FRemapTable *remap = HudFont->GetColorTranslation(CR_UNTRANSLATED);
	for(unsigned int i = MAX<int>(0, (int)(str.Len()-width));i < str.Len();++i)
	{
		VWB_DrawGraphic(HudFont->GetChar(str[i], &cwidth), x, y, MENU_NONE, remap);
		x += cwidth;
	}
}


/*
===============
=
= DrawHealth
=
===============
*/

void WolfStatusBar::DrawHealth (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.Health.Enabled) return;
	LatchNumber (StatusBarConfig.Health.X,StatusBarConfig.Health.Y,StatusBarConfig.Health.Digits,players[ConsolePlayer].health,mac,true);
}

//===========================================================================


/*
===============
=
= DrawLevel
=
===============
*/

void WolfStatusBar::DrawLevel (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.Floor.Enabled) return;
	FString str;
	str.Format("%*s", StatusBarConfig.Floor.Digits, levelInfo->FloorNumber.GetChars());
	LatchString (StatusBarConfig.Floor.X,StatusBarConfig.Floor.Y,StatusBarConfig.Floor.Digits,str);
}

//===========================================================================


/*
===============
=
= DrawLives
=
===============
*/

void WolfStatusBar::DrawLives (void)
{
	if((viewsize == 21 && ingame) || (!StatusBarConfig.Lives.Enabled) || (gamestate.difficulty->LivesCount < 0)) return;
	LatchNumber (StatusBarConfig.Lives.X,StatusBarConfig.Lives.Y,StatusBarConfig.Lives.Digits,players[ConsolePlayer].lives,mac);
}

//===========================================================================


/*
===============
=
= DrawItems
=
===============
*/

void WolfStatusBar::DrawItems (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.Items.Enabled || players[ConsolePlayer].mo == NULL) return;

	AInventory *items = players[ConsolePlayer].mo->FindInventory(ClassDef::FindClass("MacTreasureItem"));
	unsigned int amount = 0;
	if(items)
		amount = items->amount;

	LatchNumber (StatusBarConfig.Items.X,StatusBarConfig.Items.Y,StatusBarConfig.Items.Digits,amount,mac);
}

//===========================================================================

/*
===============
=
= DrawScore
=
===============
*/

void WolfStatusBar::DrawScore (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.Score.Enabled) return;
	LatchNumber (StatusBarConfig.Score.X,StatusBarConfig.Score.Y,StatusBarConfig.Score.Digits,players[ConsolePlayer].score,mac);
}

//===========================================================================

/*
==================
=
= GetArmorIcon
=
==================
*/

FTextureID WolfStatusBar::GetArmorIcon (int &points)
{
	int ap = 0;
	FTextureID icon = TexMan.GetTexture(NULL, FTexture::TEX_Null);

	if(players[ConsolePlayer].mo)
	{
		ABasicArmor *barmor = players[ConsolePlayer].mo->FindInventory<ABasicArmor>();

		if (barmor && barmor->amount > 0)
		{
			ap += barmor->amount;
			icon = barmor->icon;
			points = ap;
		}
	}

	return icon;
}

/*
==================
=
= DrawWeapon
=
==================
*/

void WolfStatusBar::DrawWeapon (void)
{
	FTextureID armorIcon;

	if((viewsize == 21 && ingame) || !StatusBarConfig.Weapon.Enabled ||
		players[ConsolePlayer].ReadyWeapon == NULL ||
		players[ConsolePlayer].ReadyWeapon->icon.isNull()
	)
		return;

	VWB_DrawGraphic(TexMan(players[ConsolePlayer].ReadyWeapon->icon), StatusBarConfig.Weapon.X, 200-(STATUSLINES-StatusBarConfig.Weapon.Y));
}

/*
==================
=
= DrawArmor
=
==================
*/

void WolfStatusBar::DrawArmor (void)
{
	int armorPoints = 0;
	FTextureID armorIcon;

	if((viewsize == 21 && ingame) || !StatusBarConfig.Armor.Enabled ||
		(armorIcon = GetArmorIcon(armorPoints)).isNull()
	)
		return;

	VWB_DrawGraphic(TexMan(armorIcon), StatusBarConfig.Armor.X, 200-(STATUSLINES-StatusBarConfig.Armor.Y));
}

/*
==================
=
= DrawArmorPoints
=
==================
*/

void WolfStatusBar::DrawArmorPoints (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.ArmorPoints.Enabled)
		return;

	int armorPoints = 0;
	GetArmorIcon(armorPoints); // do not care about icon return
	//if(armorPoints <= 0)
	//	return;

	LatchNumber (StatusBarConfig.ArmorPoints.X,StatusBarConfig.ArmorPoints.Y,StatusBarConfig.ArmorPoints.Digits,armorPoints,mac);
}

/*
==================
=
= DrawKeys
=
==================
*/

void WolfStatusBar::DrawKeys (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.Keys.Enabled) return;
	static bool extendedKeysGraphics = TexMan.CheckForTexture("STKEYS3", FTexture::TEX_Any).isValid();
	static bool emptyKeysGraphic = TexMan.CheckForTexture("STKEYS0", FTexture::TEX_Any).isValid();

	// Find keys in inventory
	unsigned int presentKeys = 0;
	if(players[ConsolePlayer].mo)
	{
		for(AInventory *item = players[ConsolePlayer].mo->inventory;item != NULL;item = item->inventory)
		{
			if(item->IsKindOf(NATIVE_CLASS(Key)))
			{
				unsigned int slot = static_cast<AKey *>(item)->KeyNumber;
				if(slot <= 4)
					presentKeys |= 1<<(slot-1);
				if(presentKeys == 15)
					break;
			}
		}
	}

	const unsigned int x = StatusBarConfig.Keys.X;
	unsigned int y = StatusBarConfig.Keys.Y;
	if (extendedKeysGraphics && (presentKeys & (1|4)) == (1|4))
		StatusDrawPic (x,y,"STKEYS5");
	else if(extendedKeysGraphics && (presentKeys & 4))
		StatusDrawPic (x,y,"STKEYS3");
	else if(presentKeys & 1)
		StatusDrawPic (x,y,"STKEYS1");
	else if(emptyKeysGraphic)
		StatusDrawPic (x,y,"STKEYS0");

	y += mac ? 20 : 16;
	if (extendedKeysGraphics && (presentKeys & (2|8)) == (2|8))
		StatusDrawPic (x,y,"STKEYS6");
	else if (extendedKeysGraphics && (presentKeys & 8))
		StatusDrawPic (x,y,"STKEYS4");
	else if (presentKeys & 2)
		StatusDrawPic (x,y,"STKEYS2");
	else if (emptyKeysGraphic)
		StatusDrawPic (x,y,"STKEYS0");
}

/*
==================
=
= DrawInventory
=
==================
*/

void WolfStatusBar::DrawInventory (void)
{
	if((viewsize == 21 && ingame) || StatusBarConfig.Inventory.empty()) return;
	if(!players[ConsolePlayer].mo) return;

	for(const auto &stInv : StatusBarConfig.Inventory)
	{
		if(!stInv.Enabled)
			continue;
		auto cls = ClassDef::FindClassTentative(stInv.ClassName, NATIVE_CLASS(Actor));
		if(!cls)
			continue;

		const unsigned int x = stInv.X;
		const unsigned int y = stInv.Y;

		auto inv = players[ConsolePlayer].mo->FindInventory(cls);
		if(inv)
			StatusDrawPic (x,y,stInv.FoundTexture);
		else
			StatusDrawPic (x,y,stInv.MissingTexture);
	}
}

//===========================================================================

/*
===============
=
= DrawAmmo
=
===============
*/

void WolfStatusBar::DrawAmmo (void)
{
	if((viewsize == 21 && ingame) || !StatusBarConfig.Ammo.Enabled ||
		!players[ConsolePlayer].ReadyWeapon || !players[ConsolePlayer].ReadyWeapon->ammo[AWeapon::PrimaryFire])
		return;

	unsigned int amount = players[ConsolePlayer].ReadyWeapon->ammo[AWeapon::PrimaryFire]->amount;
	LatchNumber (StatusBarConfig.Ammo.X,StatusBarConfig.Ammo.Y,StatusBarConfig.Ammo.Digits,amount,mac,true);
}

//===========================================================================

void WolfStatusBar::RefreshBackground(bool noborder)
{
	DBaseStatusBar::RefreshBackground(noborder);

	if(viewsize == 21 && ingame)
		return;

	VWB_DrawGraphic(TexMan("STBACK"), 0, 160);
}

void WolfStatusBar::DrawStatusBar()
{
	if(viewsize == 21 && ingame)
		return;

	VWB_DrawGraphic(TexMan("STBAR"), 0, 160);
	DrawFace ();
	DrawHealth ();
	DrawLives ();
	DrawLevel ();
	DrawAmmo ();
	DrawKeys ();
	DrawInventory ();
	DrawWeapon ();
	DrawArmor ();
	DrawArmorPoints ();
	DrawScore ();
	DrawItems ();

	DrawActors ();
}

//===========================================================================

void WolfStatusBar::SetupStatusbar()
{
	// Temporary configuration lump so that some mods can be ported to ECWolf
	// before a proper solution is created.
	// ---> WILL BE REMOVED <---

	int lastLump = 0;
	int lumpnum = 0;
	while((lumpnum = Wads.FindLump("LATCHCFG", &lastLump)) != -1)
	{
		Scanner sc(lumpnum);
		sc.ScriptMessage(Scanner::WARNING, "Utilizing temporary status bar configuration script.");

		while(sc.TokensLeft())
		{
			sc.MustGetToken(TK_Identifier);
			FString key = sc->str;
			key.ToLower();
			sc.MustGetToken('=');

			LatchConfig *var = NULL;
			FString extrakey;
			if(key.IndexOf("ammo") == 0)
			{
				extrakey = key.Mid(4);
				var = &StatusBarConfig.Ammo;
			}
			else if(key.IndexOf("floor") == 0)
			{
				extrakey = key.Mid(5);
				var = &StatusBarConfig.Floor;
			}
			else if(key.IndexOf("health") == 0)
			{
				extrakey = key.Mid(6);
				var = &StatusBarConfig.Health;
			}
			else if(key.IndexOf("items") == 0)
			{
				extrakey = key.Mid(5);
				var = &StatusBarConfig.Items;
			}
			else if(key.IndexOf("keys") == 0)
			{
				extrakey = key.Mid(4);
				var = &StatusBarConfig.Keys;
			}
			else if(key.IndexOf("lives") == 0)
			{
				extrakey = key.Mid(5);
				var = &StatusBarConfig.Lives;
			}
			else if(key.IndexOf("mugshot") == 0)
			{
				extrakey = key.Mid(7);
				var = &StatusBarConfig.Mugshot;
			}
			else if(key.IndexOf("score") == 0)
			{
				extrakey = key.Mid(5);
				var = &StatusBarConfig.Score;
			}
			else if(key.IndexOf("weapon") == 0)
			{
				extrakey = key.Mid(6);
				var = &StatusBarConfig.Weapon;
			}
			else if(key.IndexOf("armorpoints") == 0)
			{
				extrakey = key.Mid(11);
				var = &StatusBarConfig.ArmorPoints;
			}
			else if(key.IndexOf("armor") == 0)
			{
				extrakey = key.Mid(5);
				var = &StatusBarConfig.Armor;
			}
			else if(key.IndexOf("inventory") == 0)
			{
				extrakey = key.Mid(9);
				if(extrakey.Compare("enabled") == 0)
				{
					StatusBarConfig.Inventory.resize(StatusBarConfig.Inventory.size() + 1);
				}
				var = &StatusBarConfig.Inventory.back();

				auto &invVar = StatusBarConfig.Inventory.back();

				std::map<std::string, FString&> refsByProperty =
				{
					{"classname", invVar.ClassName},
					{"foundtexture", invVar.FoundTexture},
					{"missingtexture", invVar.MissingTexture},
				};

				auto refIt = refsByProperty.find(extrakey.GetChars());
				if(refIt != std::end(refsByProperty))
				{
					sc.MustGetToken(TK_StringConst);
					refIt->second = sc->str;
					continue;
				}
			}
			else if(key.Compare("showwithoutkey") == 0)
			{
				sc.MustGetToken(TK_BoolConst);
				StatusBarConfig.ShowWithoutKey = sc->boolean;
				continue;
			}
			else
				sc.ScriptMessage(Scanner::ERROR, "Unknown key '%s'.\n", key.GetChars());

			sc.MustGetToken(TK_IntConst);
			unsigned int value = sc->number;

			if(extrakey.Compare("enabled") == 0)
				var->Enabled = value;
			else if(extrakey.Compare("digits") == 0)
				var->Digits = value;
			else if(extrakey.Compare("x") == 0)
				var->X = value;
			else if(extrakey.Compare("y") == 0)
				var->Y = value;
			else
				sc.ScriptMessage(Scanner::ERROR, "Unknown key '%s'.\n", key.GetChars());
		}
	}
}
