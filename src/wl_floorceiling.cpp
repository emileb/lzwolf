#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <iostream>
#include "textures/textures.h"
#include "c_cvars.h"
#include "id_ca.h"
#include "gamemap.h"
#include "wl_def.h"
#include "wl_draw.h"
#include "wl_main.h"
#include "wl_shade.h"
#include "r_data/colormaps.h"
#include "g_mapinfo.h"
#include "thingdef/thingdef.h"

#include <climits>

extern fixed viewshift;
extern fixed viewz;

namespace Shading
{
	const BYTE *GetCMapStart (const ClassDef *littype);

	class Span
	{
	public:
		int len;
		int light;
		const ClassDef *littype;
		const byte *shades;

		explicit Span(int len_, int light_, const ClassDef *littype_) : len(len_), light(light_), littype(littype_), shades(0)
		{
		}
	};

	class Halo
	{
	public:
		typedef std::vector<Halo>::size_type Id;

		TVector2<double> C;
		double R;
		int light;
		const ClassDef *littype;

		Halo(TVector2<double> C_, double R_, int light_, const ClassDef *littype_) :
			C(C_), R(R_), light(light_), littype(littype_)
		{
		}
	};

	class Tile
	{
	public:
		typedef std::pair<int, int> Pos;
		std::vector<Halo::Id> haloIds;
	};

	int halfheight;
	fixed planeheight;
	std::vector<Span> spans;
	Span *curspan;
	std::vector<Halo> halos;
	std::vector<Tile> tiles;
	Halo::Id lastHaloId;
	std::vector<byte> rowHaloIds;
	typedef unsigned short ZoneId;
	std::map<ZoneId, AActor::ZoneLight> zoneLightMap;

	void PopulateHalos (void)
	{
		halos.clear();
		zoneLightMap.clear();
		//halos.push_back(Halo(TVector2<double>(49.5, 146.5), 0.5, 10<<3));
		//halos.push_back(Halo(TVector2<double>(49.5, 146.5), 1.0, 5<<3));

		const unsigned int mapwidth = map->GetHeader().width;
		const unsigned int mapheight = map->GetHeader().height;

		for(AActor::Iterator check = AActor::GetIterator();check.Next();)
		{
			{
				typedef AActor::HaloLightList Li;

				Li *li = check->GetHaloLightList();
				if (li)
				{
					Li::Iterator item = li->Head();
					do
					{
						Li::Iterator haloLight = item;
						if ((check->haloLightMask & (1 << haloLight->id)) != 0)
						{
							if (haloLight->light != 0 && haloLight->radius > 0.0)
							{
								const double x = FIXED2FLOAT(check->x);
								const double y = FIXED2FLOAT(check->y);
								halos.push_back(Halo(TVector2<double>(x, y), haloLight->radius, haloLight->light<<3, haloLight->littype));
							}
						}
					}
					while(item.Next());
				}
			}

			{
				typedef AActor::ZoneLightList Li;

				Li *li = check->GetZoneLightList();
				if (li)
				{
					Li::Iterator item = li->Head();
					do
					{
						Li::Iterator zoneLight = item;
						if ((check->zoneLightMask & (1 << zoneLight->id)) != 0)
						{
							if (zoneLight->light != 0)
							{
								unsigned int curx = check->x>>TILESHIFT;
								unsigned int cury = check->y>>TILESHIFT;
								MapSpot spot = map->GetSpot(curx%mapwidth, cury%mapheight, 0);
								if (spot->zone != NULL)
								{
									auto& zl = zoneLightMap[spot->zone->index];
									zl.light += zoneLight->light<<3;
									if (zoneLight->littype != nullptr)
									{
										zl.littype = zoneLight->littype;
									}
								}
							}
						}
					}
					while(item.Next());
				}
			}
		}

		const auto numtiles = mapwidth * mapheight;
		if (numtiles != tiles.size())
		{
			tiles.resize(numtiles);
		}
		std::fill(std::begin(tiles), std::end(tiles), Tile{});

		lastHaloId = 0;
		{
			typedef std::vector<Halo> HaloVec;
			const HaloVec &v = halos;
			for (HaloVec::const_iterator it = v.begin(); it != v.end(); ++it)
			{
				const Halo &h = *it;
				TVector2<int> low, high;
				(h.C - h.R).Convert(low);
				(h.C + h.R).Convert(high);

				const auto haloId = it - v.begin();

				int x;
				for (x = low.X; x <= high.X; x++)
				{
					int y;
					for (y = low.Y; y <= high.Y; y++)
					{
						if (map->IsValidTileCoordinate(x,y,0))
						{
							tiles[x+y*mapwidth].haloIds.push_back(haloId);
							lastHaloId = std::max(lastHaloId,
									static_cast<Halo::Id>(haloId + 1));
						}
					}
				}
			}
		}
		rowHaloIds = std::vector<byte>((lastHaloId+7)/8);
	}

	void PrepareConstants (int halfheight_, fixed planeheight_)
	{
		halfheight = halfheight_;
		planeheight = planeheight_;
	}

	void InsertSpan (int x1, int x2, std::vector<Span> &v, int light, const ClassDef *littype)
	{
		typedef std::vector<Span> Vec;

		while (1)
		{
			int sx = 0;
			Vec::size_type i;
			for (i = 0; i < v.size(); i++)
			{
				const Span &s = v[i];
				sx += s.len;
				if (x1 < sx)
				{
					sx -= s.len;
					break;
				}
			}
			if (i == v.size())
				break;

			if (x1 == sx)
			{
				if (x2 >= sx+v[i].len)
				{
					v[i].light += light;
					v[i].littype = littype;
					x1 = sx+v[i].len;
					if (x1 < x2)
						continue;
				}
				else // x2 < sx+v[i].len
				{
					v.insert(v.begin()+i+1, Span((sx+v[i].len)-x2,v[i].light,v[i].littype));
					v[i].len = x2-x1;
					v[i].light += light;
					v[i].littype = littype;
				}
			}
			else // x1 > sx
			{
				v.insert(v.begin()+i+1, Span(v[i].len-(x1-sx),v[i].light,v[i].littype));
				v[i].len = x1-sx;
				continue;
			}
			break;
		}
	}

	void NextY (int y, int lx, int rx, int bot)
	{
		fixed dist;
		fixed gu, gv, du, dv;
		fixed tex_step;

		const int botind = 1+((bot+1)>>1);
		const int vw = rx-lx;

		dist = ((heightnumerator<<8) / InvWallMidY(y<<3, bot));
		gu = viewx + FixedMul(dist, viewcos);
		gv = viewy - FixedMul(dist, viewsin);
		tex_step = dist / scale;
		du = FixedMul(tex_step, viewsin);
		dv = FixedMul(tex_step, viewcos);
		gu -= ((viewwidth >> 1) - lx) * du;
		gv -= ((viewwidth >> 1) - lx) * dv; // starting point (leftmost)

		// Depth fog
		const fixed tz = FixedMul(FixedDiv(r_depthvisibility, abs(planeheight)), abs(((halfheight)<<16) - ((halfheight-y)<<16)));

		spans.clear();
		spans.push_back(Span(vw, 0, NULL));

		const unsigned int mapwidth = map->GetHeader().width;
		const unsigned int mapheight = map->GetHeader().height;

		std::memset(rowHaloIds.data(), 0, rowHaloIds.size());
		{
			const fixed gu0 = gu;
			const fixed gv0 = gv;
			unsigned int oldmapx = INT_MAX, oldmapy = INT_MAX;
			unsigned int oldmapxdoor = INT_MAX;
			unsigned int oldzone = INT_MAX;
			int zonex = -1;
			unsigned int curzone = INT_MAX;
			unsigned int oldlightsector = INT_MAX;
			const MapLightSector* p_oldlightsector = NULL;
			int lightsectorx = -1;
			unsigned int curlightsector = INT_MAX;
			const MapLightSector* p_curlightsector = NULL;
			MapTile::Side doordir = MapTile::East;
			MapSpot doorspot = NULL;
			for (int x = lx; x < rx; x++)
			{
				if(y >= wallheight[x][botind]>>3)
				{
					unsigned int curx = (gu >> TILESHIFT);
					unsigned int cury = (gv >> TILESHIFT);

					if(curx != oldmapx || cury != oldmapy)
					{
						oldmapx = curx;
						oldmapy = cury;
						oldmapxdoor = INT_MAX;

						const int mapx = (int)(oldmapx%mapwidth);
						const int mapy = (int)(oldmapy%mapheight);

						const auto &ids =
							tiles[mapx+mapy*mapwidth].haloIds;
						for (auto id : ids)
						{
							rowHaloIds[id/8] |= 1<<(id&7);
						}

						MapSpot spot = map->GetSpot(mapx, mapy, 0);
						if (spot)
						{
							if (spot->tile)
							{
								if (spot->tile->offsetVertical && !spot->tile->offsetHorizontal)
								{
									doorspot = spot;
									doordir = MapTile::East;
									oldmapxdoor = gu >> (TILESHIFT-1);
									spot = doorspot->GetAdjacent(doordir, !(oldmapxdoor&1));
								}
								else if (spot->tile->offsetHorizontal && !spot->tile->offsetVertical)
								{
									doorspot = spot;
									doordir = MapTile::South;
									oldmapxdoor = gv >> (TILESHIFT-1);
									spot = doorspot->GetAdjacent(doordir, !(oldmapxdoor&1));
								}
								if (spot && spot->zone != NULL)
									curzone = spot->zone->index;
								if (spot && spot->lightsector != NULL)
								{
									curlightsector = spot->lightsector->index;
									p_curlightsector = spot->lightsector;
								}
							}
							else
							{
								if (spot->zone != NULL)
									curzone = spot->zone->index;
								if (spot->lightsector != NULL)
								{
									curlightsector = spot->lightsector->index;
									p_curlightsector = spot->lightsector;
								}
							}
						}
					}

					if (oldmapxdoor != INT_MAX)
					{
						unsigned int curxdoor = ((doordir==MapTile::South ? gv:gu) >> (TILESHIFT-1));
						if (curxdoor != oldmapxdoor)
						{
							MapSpot spot = doorspot->GetAdjacent(doordir, !(curxdoor&1));
							if (spot && spot->zone != NULL)
								curzone = spot->zone->index;
							if (spot && spot->lightsector != NULL)
							{
								curlightsector = spot->lightsector->index;
								p_curlightsector = spot->lightsector;
							}
							oldmapxdoor = INT_MAX;
						}
					}
				}
				else
				{
					curzone = INT_MAX;
					curlightsector = INT_MAX;
					p_curlightsector = NULL;
				}

				if (curlightsector != oldlightsector)
				{
					if (lightsectorx > -1 && oldlightsector != INT_MAX &&
						p_oldlightsector != NULL)
					{
						InsertSpan (lightsectorx-lx, x-lx, spans, p_oldlightsector->light, NULL);
					}
					oldlightsector = curlightsector;
					p_oldlightsector = p_curlightsector;
					lightsectorx = x;
				}
				if (curzone != oldzone)
				{
					if (zonex > -1 && oldzone != INT_MAX &&
						zoneLightMap.find((ZoneId)oldzone) != zoneLightMap.end())
					{
						const auto& zoneLight =
							zoneLightMap.find((ZoneId)oldzone)->second;
						InsertSpan (zonex-lx, x-lx, spans, zoneLight.light, zoneLight.littype);
					}
					oldzone = curzone;
					zonex = x;
				}

				gu += du;
				gv += dv;
			}

			if (lightsectorx > -1 && INT_MAX != oldlightsector && lightsectorx<rx &&
				p_oldlightsector != NULL)
			{
				InsertSpan (lightsectorx, rx, spans, p_oldlightsector->light, NULL);
			}
			if (zonex > -1 && INT_MAX != oldzone && zonex<rx &&
				zoneLightMap.find((ZoneId)oldzone) != zoneLightMap.end())
			{
				const auto& zoneLight =
					zoneLightMap.find((ZoneId)oldzone)->second;
				InsertSpan (zonex-lx, vw-1, spans, zoneLight.light, zoneLight.littype);
			}

			gu = gu0;
			gv = gv0;
		}

		// ray
		// x = S + Vt
		// where V = E-S

		// halos
		// H(i): ||x - C(i)|| <= R(i)

		// f(t) = ||S + Vt - C||
		// need (f(t))^2 <= R^2
		// f(t) = ||Vt + (S-C)||
		// (f(t))^2 = V.Vt^2 + 2V.(S-C)t + (S-C).(S-C)

		// t = (-b +- sqrt(b^2 - 4ac)) / 2a

		typedef TVector2<double> Vec2;
		const Vec2 S = Vec2(FIXED2FLOAT(gu), FIXED2FLOAT(gv));
		const Vec2 dV = Vec2(FIXED2FLOAT(du), FIXED2FLOAT(dv));
		const Vec2 E = S + dV * (double)vw;
		const Vec2 V = E-S;

		const double a = V|V;

		for (Halo::Id id = 0; id < lastHaloId; ++id)
		{
			if (!(rowHaloIds[id/8] & 1<<(id&7)))
				continue;

			const Halo &halo = halos[id];
			const Vec2 C = halo.C;
			const double R = halo.R;

			const double b = 2*(V|(S-C));
			const double c = ((S-C)|(S-C))-R*R;

			const double desc = b*b-4*a*c;
			if (desc > 0)
			{
				const double sqdesc = sqrt(desc);
				const double t1 = std::max((-b - sqdesc)/(2*a),0.0);
				const double t2 = std::min((-b + sqdesc)/(2*a),1.0);

				const int x1 = t1*vw;
				const int x2 = t2*vw;

				if (x1<x2)
					InsertSpan (x1, x2, spans, halo.light, halo.littype);
			}
		}

		for (std::vector<Span>::size_type i = 0; i < spans.size(); i++)
		{
			Span &span = spans[i];

			const int shade = LIGHT2SHADE(gLevelLight + r_extralight + span.light);
			const BYTE *cmapstart = GetCMapStart (span.littype);
			span.shades = &cmapstart[GETPALOOKUP(MAX(tz, MINZ), shade)<<8];
		}

		curspan = &spans[0];
	}

	const byte *ShadeForPix ()
	{
		const byte *curshades = curspan->shades;
		curspan->len--;
		if (!curspan->len)
			curspan++;
		return curshades;
	}

	const ClassDef *LitForPix ()
	{
		const ClassDef *curlit = curspan->littype;
		curspan->len--;
		if (!curspan->len)
			curspan++;
		return curlit;
	}

	int LightForIntercept (fixed xintercept, fixed yintercept, const ClassDef* &littype)
	{
		unsigned int curx,cury;

		curx = xintercept>>TILESHIFT;
		cury = yintercept>>TILESHIFT;

		const unsigned int mapwidth = map->GetHeader().width;
		const unsigned int mapheight = map->GetHeader().height;

		int light = 0;
		typedef std::vector<Halo::Id> Vec;
		const Vec &v = tiles[(curx%mapwidth)+(cury%mapheight)*mapwidth].haloIds;
		if (v.size() > 0)
		{
			const double x = FIXED2FLOAT(xintercept);
			const double y = FIXED2FLOAT(yintercept);

			typedef TVector2<double> Vec2;
			const Vec2 P(x,y);

			for (Vec::const_iterator it = v.begin(); it != v.end(); ++it)
			{
				const Halo &halo = halos[*it];

				const Vec2 C = halo.C;
				const double R = halo.R;

				if (((P-C)|(P-C)) <= R*R)
					light += halo.light;
			}
		}

		MapSpot spot = map->GetSpot(curx%mapwidth, cury%mapheight, 0);
		if (spot->tile)
		{
			unsigned int oldmapxdoor;
			MapTile::Side doordir;
			MapSpot doorspot;

			if (spot->tile->offsetVertical && !spot->tile->offsetHorizontal)
			{
				doorspot = spot;
				doordir = MapTile::East;
				oldmapxdoor = xintercept >> (TILESHIFT-1);
				spot = doorspot->GetAdjacent(doordir, !(oldmapxdoor&1));
			}
			else if (spot->tile->offsetHorizontal && !spot->tile->offsetVertical)
			{
				doorspot = spot;
				doordir = MapTile::South;
				oldmapxdoor = yintercept >> (TILESHIFT-1);
				spot = doorspot->GetAdjacent(doordir, !(oldmapxdoor&1));
			}
		}
		if (spot && spot->zone != NULL &&
				zoneLightMap.find(spot->zone->index) != zoneLightMap.end())
		{
			const auto &zl = zoneLightMap.find(spot->zone->index)->second;
			light += zl.light;
			littype = zl.littype;
		}
		if (spot && spot->lightsector != NULL)
			light += spot->lightsector->light;

		return light;
	}

	const BYTE *GetCMapStart (const ClassDef *littype)
	{
		// fast path
		if(!littype)
			return &realcolormaps[0];
		if(littype->CMapStart)
			return littype->CMapStart;

		// slow path
		const FName cmapname = (littype &&
			littype->FadeCMapName != FName()) ?
			littype->FadeCMapName :
			FName();
		const BYTE *cmapstart = 
			&realcolormaps[R_ColormapNumForName(cmapname.GetChars())*256*NUMCOLORMAPS];
		return cmapstart;
	}
}

static inline bool R_PixIsTrans(byte col, const std::pair<bool, byte> &trans)
{
	return trans.first && col == trans.second;
}

static void R_DrawPlane(byte *vbuf, unsigned vbufPitch, TWallHeight min_wallheight, int halfheight, fixed planeheight, std::pair<bool, byte> trans = std::make_pair(false, 0x00))
{
	fixed dist;                                // distance to row projection
	fixed tex_step;                            // global step per one screen pixel
	fixed gu, gv, du, dv;                      // global texture coordinates
	const byte *tex = NULL;
	int texwidth, texheight;
	fixed texxscale, texyscale;
	FTextureID lasttex;
	byte *tex_offset;
	bool useOptimized = false;

	if(planeheight == 0) // Eye level
		return;
	
	TWallHeight y0{{min_wallheight[0]>>3,min_wallheight[1]>>3,min_wallheight[2]>>3}};

	const unsigned int mapwidth = map->GetHeader().width;
	const unsigned int mapheight = map->GetHeader().height;

	fixed planenumerator = FixedMul(heightnumerator, planeheight);
	const bool floor = planenumerator < 0;
	int tex_offsetPitch;
	if(floor)
	{
		unsigned bot_offset0 = vbufPitch * (halfheight + y0[2]);
		tex_offset = vbuf + bot_offset0;
		tex_offsetPitch = vbufPitch-viewwidth;
		planenumerator *= -1;
	}
	else
	{
		unsigned top_offset0 = vbufPitch * (halfheight - y0[1] - 1);
		tex_offset = vbuf + top_offset0;
		tex_offsetPitch = -viewwidth-vbufPitch;
	}

	Shading::PrepareConstants (halfheight, planeheight);

	const int viewxTile = viewx>>FRACBITS;
	const int viewxFrac = (viewx&(FRACUNIT-1))<<8; // 8.24
	const int viewyTile = viewy>>FRACBITS;
	const int viewyFrac = (viewy&(FRACUNIT-1))<<8; // 8.24

	unsigned int oldmapx = INT_MAX, oldmapy = INT_MAX;
	const byte* curshades = NormalLight.Maps;

	const int bot = (floor ? 1 : -1);
	const int botind = 1+((bot+1)>>1);
	int y0bot = y0[botind];

	// draw horizontal lines
	for(int y = y0bot;y < halfheight; ++y, tex_offset += tex_offsetPitch)
	{
		if(y < 0)
		{
			tex_offset += viewwidth;
			continue;
		}

		// Shift in some extra bits so that we don't get spectacular round off.
		dist = ((heightnumerator<<8) / InvWallMidY(y<<3, bot))<<8;
		gu =  viewxFrac + FixedMul(dist, viewcos);
		gv = -viewyFrac + FixedMul(dist, viewsin);
		tex_step = dist / scale;
		du =  FixedMul(tex_step, viewsin);
		dv = -FixedMul(tex_step, viewcos);
		gu -= (viewwidth >> 1) * du;
		gv -= (viewwidth >> 1) * dv; // starting point (leftmost)

		curshades = NormalLight.Maps;
		Shading::NextY (y, 0, viewwidth, bot);

		lasttex.SetInvalid();
		oldmapx = oldmapy = INT_MAX;
		tex = NULL;

		for(unsigned int x = 0;x < (unsigned)viewwidth; ++x, ++tex_offset)
		{
			if(y >= wallheight[x][botind]>>3)
			{
				unsigned int curx = viewxTile + (gu >> (TILESHIFT+8));
				unsigned int cury = viewyTile + (-(gv >> (TILESHIFT+8)) - 1);

				if(curx != oldmapx || cury != oldmapy)
				{
					oldmapx = curx;
					oldmapy = cury;
					const MapSpot spot = map->GetSpot(oldmapx%mapwidth, oldmapy%mapheight, 0);

					if(spot->sector)
					{
						FTextureID curtex = spot->sector->texture[floor ? MapSector::Floor : MapSector::Ceiling];
						if (curtex.isValid())
						{
							if(curtex != lasttex)
							{
								FTexture * const texture = TexMan(curtex);
								lasttex = curtex;
								tex = texture->GetPixels();
								texwidth = texture->GetWidth();
								texheight = texture->GetHeight();
								texxscale = texture->xScale>>10;
								texyscale = -texture->yScale>>10;

								useOptimized = texwidth == 64 && texheight == 64 && texxscale == FRACUNIT>>10 && texyscale == -FRACUNIT>>10;
							}
						}
						else
						{
							tex = NULL;
							lasttex.SetInvalid();
						}
					}
					else
					{
						tex = NULL;
						lasttex.SetInvalid();
					}
				}

				curshades = Shading::ShadeForPix ();

				if(tex)
				{
					if(useOptimized)
					{
						const int u = (gu>>18) & 63;
						const int v = (-gv>>18) & 63;
						const unsigned texoffs = (u * 64) + v;
						if (!R_PixIsTrans(tex[texoffs], trans))
							*tex_offset = curshades[tex[texoffs]];
					}
					else
					{
						const int u = (FixedMul((viewxTile<<16)+(gu>>8)-512, texxscale)) & (texwidth-1);
						const int v = (FixedMul((viewyTile<<16)+(gv>>8)+512, texyscale)) & (texheight-1);
						const unsigned texoffs = (u * texheight) + v;
						if (!R_PixIsTrans(tex[texoffs], trans))
							*tex_offset = curshades[tex[texoffs]];
					}
				}
			}
			else
			{
				Shading::ShadeForPix ();
			}
			gu += du;
			gv += dv;
		}
	}
}

// Textured Floor and Ceiling by DarkOne
// With multi-textured floors and ceilings stored in lower and upper bytes of
// according tile in third mapplane, respectively.
void DrawFloorAndCeiling(byte *vbuf, unsigned vbufPitch, TWallHeight min_wallheight)
{
	const int halfheight = (viewheight >> 1) - viewshift;

	const byte skyfloorcol = (gameinfo.parallaxskyfloorcolor >= 256 ?
		(byte)(gameinfo.parallaxskyfloorcolor&0xff) : 0xff);
	const byte skyceilcol = (gameinfo.parallaxskyceilcolor >= 256 ?
		(byte)(gameinfo.parallaxskyceilcolor&0xff) : 0xff);

	const bool skyEnabled = levelInfo->SkyEnabled();
	std::pair<bool, byte> floortrans(skyEnabled, skyfloorcol);
	std::pair<bool, byte> ceiltrans(skyEnabled, skyceilcol);

	R_DrawPlane(vbuf, vbufPitch, min_wallheight, halfheight, viewz, floortrans);
	R_DrawPlane(vbuf, vbufPitch, min_wallheight, halfheight, viewz+(map->GetPlane(0).depth<<FRACBITS), ceiltrans);
}
