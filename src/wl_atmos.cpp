#include "version.h"

#include "wl_def.h"
#include "wl_main.h"
#include "wl_draw.h"
#include "wl_agent.h"
#include "wl_play.h"
#include "id_ca.h"
#include "id_vh.h"
#include "g_mapinfo.h"
#include "textures/textures.h"
#include "v_video.h"
#include "m_random.h"
#include "lw_vec.h"
#include "c_console.h"

#define RAINSCALING

#define mapheight (map->GetHeader().height)
#define mapwidth (map->GetHeader().width)

CVAR (Bool, at_hqsnow, false, CVAR_ARCHIVE)
CVAR (Bool, at_notilecheck, false, CVAR_ARCHIVE)

static constexpr auto MINDIST = 0x5800l;

uint32_t rainpos = 0;

typedef struct {
    int32_t x, y, z;
    int32_t waveamp, wavefineangle;
} point3d_t;

#define MAXPOINTS 3600
#define HALFMAXPOINTS (MAXPOINTS/2)
point3d_t points[MAXPOINTS];

byte moon[100]={
	0,  0, 27, 18, 15, 16, 19, 29,  0,  0,
	0, 22, 16, 15, 15, 16, 16, 18, 24,  0,
	27, 17, 15, 17, 16, 16, 17, 17, 18, 29,
	18, 15, 15, 15, 16, 16, 17, 17, 18, 20,
	16, 15, 15, 16, 16, 17, 17, 18, 19, 21,
	16, 15, 17, 20, 18, 17, 18, 18, 20, 22,
	19, 16, 18, 19, 17, 17, 18, 19, 22, 24,
	28, 19, 17, 17, 17, 18, 19, 21, 25, 31,
	0, 23, 18, 19, 18, 20, 22, 24, 28,  0,
	0,  0, 28, 21, 20, 22, 28, 30,  0,  0 };

static inline int32_t LABS(int32_t x)
{
    return ((int32_t)x>0?x:-x);
}

void Init3DPoints()
{
    const auto halfviewheight = viewheight >> 1;
    int hvheight = halfviewheight;
    for(int i = 0; i < MAXPOINTS; i++)
    {
        point3d_t *pt = &points[i];
        pt->x = 16384 - (rand() & 32767);
        pt->z = 16384 - (rand() & 32767);
        pt->waveamp = (rand() & 0x00ffl);
        pt->wavefineangle = (rand() % FINEANGLES);
        float len = sqrt((float)pt->x * pt->x + (float)pt->z * pt->z);
        int j=50;
        do
        {
            pt->y = 1024 + (rand() & 8191);
            j--;
        }
        while(j > 0 && (float)pt->y * 256.F / len >= hvheight);
    }
}

void DrawHighQualityStarSky(byte *vbuf, uint32_t vbufPitch)
{
    const auto halfviewheight = viewheight >> 1;
	int hvheight = halfviewheight;
	int hvwidth = viewwidth >> 1;
	int halves = 2;

	byte *ptr = vbuf;
	int i;
	for(i = 0; i < hvheight * halves; i++, ptr += vbufPitch)
		memset(ptr, 0, viewwidth);

	for(i = 0; i < HALFMAXPOINTS*halves; i++)
	{
		const int which_half = (i / HALFMAXPOINTS);
		point3d_t *pt = &points[i % HALFMAXPOINTS];
		int32_t x = pt->x * viewcos + pt->z * viewsin;
		int32_t y = pt->y << 16;
		int32_t z = (pt->z * viewcos - pt->x * viewsin) >> 8;
		if(z <= 0) continue;
		int shade = z >> 18;
		if(shade > 15) continue;
		int32_t xx = x / z + hvwidth;
		int32_t yy = hvheight - (y / z) * (which_half ? -1 : 1);
		int halfy0 = 0, halfy1 = hvheight;
		if(which_half)
		{
			halfy0 = hvheight;
			halfy1 = viewheight;
		}
		if(xx >= 1 && xx < viewwidth - 1 && yy >= halfy0 + 1 && yy < halfy1 - 1)
		{
			vbuf[yy * vbufPitch + xx] = shade + 15;
			if (15 - shade > 1)
			{
				shade = 15 - ((15 - shade) >> 1);
				vbuf[yy * vbufPitch + xx + 1] =
					vbuf[yy * vbufPitch + xx - 1] =
					vbuf[(yy + 1) * vbufPitch + xx] =
					vbuf[(yy - 1) * vbufPitch + xx] = shade + 15;
			}
		}
	}

	FTextureID picnum = TexMan.CheckForTexture ("FULLMOON", FTexture::TEX_Any);
	if(!picnum.isValid())
		return;
	FTexture *picture = TexMan(picnum);

	{
		int pointInd = levelInfo->Atmos[3];
		pointInd = (
			pointInd == 1 || pointInd < 0 || pointInd >= HALFMAXPOINTS) ? 10 : pointInd;

		point3d_t *pt = &points[pointInd];
		int32_t x = pt->x * viewcos + pt->z * viewsin;
		int32_t y = pt->y << 16;
		int32_t z = (pt->z * viewcos - pt->x * viewsin) >> 8;
		if(z <= 0) return;
		int32_t xx = x / z + hvwidth;
		int32_t yy = hvheight - y / z;
		xx += viewscreenx - 15;
		yy += viewscreeny - 15;
		if(xx + 30 >= 0 && xx < (int32_t)screenWidth &&
			yy + 30 >= 0 && yy < (int32_t)screenHeight)
		{
			double x = xx, y = yy;

			screen->Lock(false);
			double wd = picture->GetScaledWidthDouble();
			double hd = picture->GetScaledHeightDouble();

			screen->DrawTexture(picture, x, y,
				DTA_DestWidthF, wd,
				DTA_DestHeightF, hd,
				DTA_ClipLeft, viewscreenx,
				DTA_ClipRight, viewscreenx + viewwidth,
				DTA_ClipTop, viewscreeny,
				DTA_ClipBottom, viewscreeny + viewheight,
				TAG_DONE);
			screen->Unlock();
		}
	}
}

void DrawStarSky(byte *vbuf, uint32_t vbufPitch)
{
	int hvheight = viewheight >> 1;
	int hvwidth = viewwidth >> 1;
	int halves = 2;

	byte *ptr = vbuf;
	int i;
	for(i = 0; i < hvheight * halves; i++, ptr += vbufPitch)
		memset(ptr, 0, viewwidth);

	for(i = 0; i < HALFMAXPOINTS*halves; i++)
	{
		const int which_half = (i / HALFMAXPOINTS);
		point3d_t *pt = &points[(which_half ? HALFMAXPOINTS : 0) + (i % HALFMAXPOINTS)];
		int32_t x = pt->x * viewcos + pt->z * viewsin;
		int32_t y = pt->y << 16;
		int32_t z = (pt->z * viewcos - pt->x * viewsin) >> 8;
		if(z <= 0) continue;
		int shade = z >> 18;
		if(shade > 15) continue;
		int32_t xx = x / z + hvwidth;
		int32_t yy = hvheight - (y / z) * (which_half ? -1 : 1);
		int halfy0 = 0, halfy1 = hvheight;
		if(which_half)
		{
			halfy0 = hvheight;
			halfy1 = viewheight;
		}
		if(xx >= 0 && xx < viewwidth && yy >= halfy0 && yy < halfy1)
		{
			vbuf[yy * vbufPitch + xx] = shade + 15;
		}
	}

	int32_t x = 16384 * viewcos + 16384 * viewsin;
	int32_t z = (16384 * viewcos - 16384 * viewsin) >> 8;
	if(z <= 0)
		return;
	int32_t xx = x / z + hvwidth;
	int32_t yy = hvheight - ((hvheight - (hvheight >> 3)) << 22) / z;
	if(xx > -10 && xx < viewwidth)
	{
		int stopx = 10, starty = 0, stopy = 10;
		i = 0;
		if(xx < 0) i = -xx;
		if(xx > viewwidth - 11) stopx = viewwidth - xx;
		if(yy < 0) starty = -yy;
		if(yy > viewheight - 11) stopy = viewheight - yy;
		for(; i < stopx; i++)
			for(int j = starty; j < stopy; j++)
				vbuf[(yy + j) * vbufPitch + xx + i] = moon[j * 10 + i];
	}
}

static void DrawRainSplash(byte *vbuf, uint32_t vbufPitch, int x, int y,
                           int rainlenyy, byte col, int frame)
{
    static const char* s_splash_art[9] =
    {
        "..."
        ".#."
        "...",

        ".#."
        "#.#"
        ".#.",

        "#.#"
        "..."
        "#.#",

        "....."
        "..#.."
        ".###."
        "..#.."
        ".....",

        "....."
        ".###."
        "#...#"
        ".###."
        ".....",

        "....."
        ".#.#."
        "#...#"
        ".#.#."
        ".....",

        "......."
        "......."
        "...#..."
        "..###.."
        "...#..."
        "......."
        ".......",

        "......."
        "......."
        "..###.."
        ".#...#."
        "..###.."
        "......."
        ".......",

        "......."
        ".#.#.#."
        "......."
        "#.....#"
        "......."
        ".#.#.#."
        ".......",
    };
    const int artind = std::max(0, std::min(2, ((rainlenyy-1) / 2) - 1));
    const char* art = s_splash_art[frame+(artind*3)];
    const int w = (artind+1)*2 + 1;

    int ix, iy;
    for(ix = 0; ix < w; ++ix)
    {
        int xx = x - (w/2) + ix;
        for(iy = 0; iy < w; ++iy)
        {
            int yy = y - (w/2) + iy;
            if(art[iy*w + ix] == '#' && xx >= 0 && xx < viewwidth &&
               yy > 0 && yy < viewheight)
            {
                vbuf[yy * vbufPitch + xx] = col;
            }
        }
    }
}

void DrawRain(byte *vbuf, uint32_t vbufPitch, byte *zbuf, uint32_t zbufPitch)
{
    fixed dist;                                // distance to row projection
    fixed tex_step;                            // global step per one screen pixel
    fixed gu, gv, floorx, floory;              // global texture coordinates
    fixed px = (players[0].camera->y + FixedMul(0x7900, viewsin)) >> 6;
    fixed pz = (players[0].camera->x - FixedMul(0x7900, viewcos)) >> 6;
    int32_t ax, az, x, y, z, xx, yy, height, actheight;
    int shade;
    const auto halfviewheight = viewheight >> 1;
    int hvheight = halfviewheight;
    int hvwidth = viewwidth >> 1;

    rainpos -= (tics * 1100);

#ifdef RAINSCALING
    int32_t y2, yy2;
    int rainlenyy;

    const auto raindropmove = 0x4000;

    const int rainwid = std::max((unsigned int)1, scaleFactorX/2);
    const int rainminlen = scaleFactorX*2;
    const int rainmaxlen = scaleFactorX*5;
#endif

    for(int i = 0; i < HALFMAXPOINTS; i++)
    {
        point3d_t *pt = &points[i];
        ax = pt->x + px;
        ax = 0x1fff - (ax & 0x3fff);
        az = pt->z + pz;
        az = 0x1fff - (az & 0x3fff);
        x = ax * viewcos + az * viewsin;

        y = (((pt->y << 6) + rainpos) & 0x0ffff) - 0x8000;

        bool showsplash=false;
        if(y+0x8000<raindropmove)
        {
            showsplash=true;
        }
        else
        {
            y = FixedDiv((y+0x8000)-raindropmove,TILEGLOBAL-raindropmove)-0x8000;
        }

        z = (az * viewcos - ax * viewsin) >> 8;
        if(z <= 0) continue;
        shade = z >> 17;
        if(shade > 13) continue;
        auto dir = lwlib::vec2f(viewcos / 65536.0, -viewsin / 65536.0);
        auto viewpt = lwlib::vec2f((z>>2)/65536.0, (x>>10)/65536.0);
        auto eye = lwlib::vec2f(viewx / 65536.0, viewy / 65536.0);
        auto p = eye + viewpt.v[0] * dir + viewpt.v[1] * vec_rot90(dir);
        int32_t obx = (int32_t)(p.v[0]*TILEGLOBAL);
        int32_t oby = (int32_t)(p.v[1]*TILEGLOBAL);
        //xx = x*scale/z + hvwidth;
        //xx = (int32_t)(viewpt.v[1]*scale/viewpt.v[0])+hvwidth;
        auto compute_xx_yy = [obx, oby, zbuf, zbufPitch, halfviewheight](
                int32_t y, bool &bail)
        {
            int32_t xx, yy;

            fixed nx,ny;
            nx = FixedMul(obx-viewx,viewcos)-FixedMul(oby-viewy,viewsin);
            ny = FixedMul(oby-viewy,viewcos)+FixedMul(obx-viewx,viewsin);

            if (nx<MINDIST)                 // too close, don't overflow the divide
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            xx = (word)(centerx + ny*scale/nx);
            if(!(xx >= 0 && xx < viewwidth))
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            unsigned scale;
            int upperedge;

            auto viewheight = (word)(heightnumerator/(nx>>8));
            if(wallheight[xx][0]>viewheight)
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            scale=viewheight>>3;                 // low three bits are fractional
            if(!scale)
            {
                bail=true;
                return std::make_pair(0, 0);
            }
            upperedge=halfviewheight-scale;
            //yy = upperedge+scale*2;
            yy = lwlib::lerpi(upperedge+scale*2, upperedge, y+0x8000, TILEGLOBAL);
            if(!(yy > 0 && yy < viewheight))
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            auto zdepth=std::min(scale*255/halfviewheight,255u);

            //if(zdepth < zbuf[xx*zbufPitch+yy])
            //{
            //    bail=true;
            //    return std::make_pair(0, 0);
            //}

            return std::make_pair(xx,yy);
        };
        bool bail = false;
        auto xx_yy = compute_xx_yy(y, bail);
        if(bail)
            continue;
        xx = xx_yy.first;
        yy = xx_yy.second;
#ifdef RAINSCALING
        rainlenyy = LABS((raindropmove << 11) / z);
        rainlenyy = std::min(rainlenyy, (int)(20*scaleFactorY));
        rainlenyy = std::max(rainlenyy, (int)(scaleFactorY*3));
        rainlenyy = lwlib::lerpi(rainminlen, rainmaxlen+1,
                                 (rainlenyy-(scaleFactorY*3)),
                                 (20*scaleFactorY)-(scaleFactorY*3)+1);
        //printf("yylen=%d rainlenyy=%d shade=%d viewpt.x=%.3f\n", LABS(yy2-yy), rainlenyy, shade, viewpt.v[0]);
#endif
        height = (heightnumerator << 10) / z;
        //if(actheight < 0) actheight = -actheight;
        //if(actheight < (wallheight[xx] >> 3) && height < wallheight[xx]) continue;

        // Find the rain's tile coordinate
        floorx = (int32_t(p.v[0]*TILEGLOBAL)>>TILESHIFT)%mapwidth;
        floory = (int32_t(p.v[1]*TILEGLOBAL)>>TILESHIFT)%mapheight;

        const MapSpot spot = map->GetSpot(floorx, floory, 0);
        if(spot->sector == nullptr ||
                !spot->sector->texture[MapSector::Ceiling].isValid())
        {
#ifdef RAINSCALING
            if(!showsplash)
            {
                if(xx - rainwid > 0 && xx < viewwidth &&
                   yy - rainlenyy > 0 && yy < viewheight)
                {
                    for(int j = 0; j < rainwid; j++)
                    {
                        for(int k = 0; k < rainlenyy; k++)
                        {
                            vbuf[(yy - k) * vbufPitch + xx - j] = shade+
                                lwlib::lerpi(15,18,k,rainlenyy);
                        }
                    }
                }
            }
            else
            {
                //(y+0x8000<raindropmove)
                const auto ydist = raindropmove;
                auto frame = (y+0x8000)/(ydist/3);
                frame = lwlib::clip((int)frame, 0, 2);
                bool bail = false;
                auto xx_yy = compute_xx_yy(-0x8000, bail);
                if(bail)
                    continue;
                xx = xx_yy.first;
                yy = xx_yy.second;
                DrawRainSplash(vbuf, vbufPitch, xx, yy, rainlenyy,
                            shade+15, frame);
            }
#else
            vbuf[yy * vbufPitch + xx] = shade+15;
            vbuf[(yy - 1) * vbufPitch + xx] = shade+16;
            if(yy > 2)
                vbuf[(yy - 2) * vbufPitch + xx] = shade+17;
#endif
        }
    }
}

fixed SnowWindAmplitude(uint32_t windtics)
{
    fixed windamp = 0;

    fixed t = 0;
    static const struct windparms
    {
        uint32_t Stics, Etics;
        fixed St, Et;
    } 
    wp_map[] =
    {
        { 600, 1000, 0x8000, 0x0000 },
        { 400, 800,  0x8000, 0x8000 },
        { 0,   400,  0x0000, 0x8000 },
    };
    int i;
    for (i = 0; (std::size_t)i < sizeof(wp_map)/sizeof(wp_map[0]); i++)
    {
        const struct windparms *wp = &wp_map[i];
        if (windtics >= wp->Stics && windtics <= wp->Etics)
        {
            t = ((windtics - wp->Stics) << 16l) / (wp->Etics - wp->Stics);
            t = lwlib::lerpi(wp->St, wp->Et, t, 0xffffl);
            break;
        }
    }
    windamp = (finesine[(((t * FINEANGLES) >> 16l) + ANG270) % FINEANGLES] +
        (1<<16l)) / 2;
    windamp = FixedMul(windamp, 0x2000l);

    return windamp;
}

static FRandom pr_snow("Snow");
void DrawSnow(byte *vbuf, uint32_t vbufPitch, byte *zbuf, uint32_t zbufPitch)
{
    fixed dist;                                // distance to row projection
    fixed tex_step;                            // global step per one screen pixel
    fixed gu, gv, floorx, floory;              // global texture coordinates
    fixed px = (players[0].camera->y + FixedMul(0x7900, viewsin)) >> 6;
    fixed pz = (players[0].camera->x - FixedMul(0x7900, viewcos)) >> 6;
    int32_t ax, az, x, y, z, xx, yy, height, actheight;
    int shade;
    const auto halfviewheight = viewheight >> 1;
    int hvheight = halfviewheight;
    int hvwidth = viewwidth >> 1;
    point3d_t wavept;

    const bool hqsnow = (at_hqsnow || (levelInfo->Atmos[4] != 0));

    FTexture* picture = nullptr;
    if(hqsnow)
    {
        FTextureID picnum = TexMan.CheckForTexture("SNOWBALL", FTexture::TEX_Any);
        if (!picnum.isValid())
        {
            return;
        }
        picture = TexMan(picnum);
    }

    static uint32_t windtics = 2000;
    static uint32_t maxwindtics = 2000;
    static int32_t windangle = 0;
    windtics -= tics;
    if (windtics > maxwindtics)
    {
        maxwindtics = pr_snow(3500-1500) + 1500;
        windangle = pr_snow(FINEANGLES);
        windtics = maxwindtics;
    }

    rainpos -= (tics * 230);
    for(int i = 0; i < HALFMAXPOINTS; i++)
    {
        wavept = points[i];

        fixed windamp = 0;
        if ((int)windtics < 1200 - (i * 200 / HALFMAXPOINTS))
        {
            windamp = SnowWindAmplitude(windtics - 200 + (i * 200 / HALFMAXPOINTS));
        }

        {
            fixed t = (((wavept.y << 6) + rainpos) & 0x0ffff);
            fixed amp = finesine[(t * FINEANGLES) >> 16l];
            amp = FixedMul(amp, wavept.waveamp);
            wavept.x = wavept.x + FixedMul(amp, finecosine[wavept.wavefineangle]);
            wavept.z = wavept.z - FixedMul(amp, finesine[wavept.wavefineangle]);
            wavept.x = wavept.x + FixedMul(windamp, finecosine[windangle]);
            wavept.z = wavept.z - FixedMul(windamp, finesine[windangle]);
        }
        point3d_t *pt = &wavept;
        ax = pt->x + px;
        ax = 0x1fff - (ax & 0x3fff);
        az = pt->z + pz;
        az = 0x1fff - (az & 0x3fff);
        x = ax * viewcos + az * viewsin;
        y = (((pt->y << 6) + rainpos) & 0x0ffff) - 0x8000;
        z = (az * viewcos - ax * viewsin) >> 8;
        if(z <= 0) continue;
        shade = z >> 17;
        if(shade > 13) continue;
        auto dir = lwlib::vec2f(viewcos / 65536.0, -viewsin / 65536.0);
        auto viewpt = lwlib::vec2f((z>>2)/65536.0, (x>>10)/65536.0);
        auto eye = lwlib::vec2f(viewx / 65536.0, viewy / 65536.0);
        auto p = eye + viewpt.v[0] * dir + viewpt.v[1] * vec_rot90(dir);
        int32_t obx = (int32_t)(p.v[0]*TILEGLOBAL);
        int32_t oby = (int32_t)(p.v[1]*TILEGLOBAL);
        //xx = x*scale/z + hvwidth;
        //xx = (int32_t)(viewpt.v[1]*scale/viewpt.v[0])+hvwidth;
        auto compute_xx_yy = [obx, oby, zbuf, zbufPitch, halfviewheight](
                int32_t y, bool &bail)
        {
            int32_t xx, yy;

            fixed nx,ny;
            nx = FixedMul(obx-viewx,viewcos)-FixedMul(oby-viewy,viewsin);
            ny = FixedMul(oby-viewy,viewcos)+FixedMul(obx-viewx,viewsin);

            if (nx<MINDIST)                 // too close, don't overflow the divide
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            xx = (word)(centerx + ny*scale/nx);
            if(!(xx >= 0 && xx < viewwidth))
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            unsigned scale;
            int upperedge;

            auto viewheight = (word)(heightnumerator/(nx>>8));
            if(wallheight[xx][0]>viewheight)
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            scale=viewheight>>3;                 // low three bits are fractional
            if(!scale)
            {
                bail=true;
                return std::make_pair(0, 0);
            }
            upperedge=halfviewheight-scale;
            //yy = upperedge+scale*2;
            yy = lwlib::lerpi(upperedge+scale*2, upperedge, y+0x8000, TILEGLOBAL);
            if(!(yy > 0 && yy < viewheight))
            {
                bail=true;
                return std::make_pair(0, 0);
            }

            auto zdepth=std::min(scale*255/halfviewheight,255u);

            //if(zdepth < zbuf[xx*zbufPitch+yy])
            //{
            //    bail=true;
            //    return std::make_pair(0, 0);
            //}

            return std::make_pair(xx,yy);
        };
        bool bail = false;
        auto xx_yy = compute_xx_yy(y, bail);
        if(bail)
            continue;
        xx = xx_yy.first;
        yy = xx_yy.second;

        // Find the rain's tile coordinate
        floorx = (int32_t(p.v[0]*TILEGLOBAL)>>TILESHIFT)%mapwidth;
        floory = (int32_t(p.v[1]*TILEGLOBAL)>>TILESHIFT)%mapheight;

        if(xx > 0 && xx < viewwidth-1 && yy > 0 && yy < viewheight-1)
        {
            // Is there a ceiling tile?
            const MapSpot spot = map->GetSpot(floorx, floory, 0);
            if(!at_notilecheck && spot->sector != nullptr &&
                    spot->sector->texture[MapSector::Ceiling].isValid())
            {
                continue;
            }

            if(!hqsnow)
            {
                if(shade < 10)
                {
                    vbuf[yy * vbufPitch + xx] = shade+17;
                    vbuf[yy * vbufPitch + xx - 1] = shade+16;
                    vbuf[(yy - 1) * vbufPitch + xx] = shade+16;
                    vbuf[(yy - 1) * vbufPitch + xx - 1] = shade+15;
                }
                else
                {
                    vbuf[yy * vbufPitch + xx] = shade+15;
                }
            }

            if (hqsnow && xx >= 0 && xx < (int32_t)screenWidth && yy >= 0 &&
                yy < (int32_t)screenHeight && picture)
            {
                double x = xx, y = yy;

                screen->Lock(false);
                auto compute_scale = [&](auto a, auto b) {
                    double t = shade;
                    t = t/10;
                    t = std::min(t, 1.0);
                    return a * (1-t) + b * t;
                };

                auto scale = compute_scale(0.7, 0.1);
                double stx = 0;
                double sty = 0;
                double stw = std::max(4 * scale, 1.0);
                double sth = 0;
                screen->VirtualToRealCoords(stx, sty, stw, sth, 320, 200, true, true);
                double wd = stw;
                double hd = stw;

                const DWORD alpha = shade*175/12;
                auto shade = MAKEARGB(alpha, 0, 0, 0);
                screen->DrawTexture(picture, x, y, DTA_ColorOverlay, shade,
                                    DTA_DestWidthF, wd,
                                    DTA_DestHeightF, hd, DTA_ClipLeft, viewscreenx,
                                    DTA_ClipRight, viewscreenx + viewwidth, DTA_ClipTop,
                                    viewscreeny, DTA_ClipBottom, viewscreeny + viewheight,
                                    TAG_DONE);
                screen->Unlock();
            }
        }
    }
}
