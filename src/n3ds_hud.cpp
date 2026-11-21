// Status bar (HUD), map and frame counter on the 3DS bottom screen.

#include <math.h>
#include <stdio.h>
#include "wl_def.h"
#include "n3ds_hud.h"
#include "n3ds_wide.h"
#include "n3ds_input.h"
#include "n3ds_keyboard.h"
#include "n3ds_weapons.h"

static SDL_Surface *hud = NULL;     // same size as screenBuffer, 8 bit
static boolean hudready = false;
int hudpos = hudpos_top;
boolean showmap = true;
boolean showfps = false;
boolean weaponbar = false;
int mapzoom = 2;
bool showenemies = false;
bool showitems = false;
bool showlegend = false;

// Tiles seen since the level started: the original has no map, so this one
// shows only what the player has seen (spotvis, cleared every frame).
byte seen[MAPSIZE][MAPSIZE];

boolean N3DS_HudVisible (void)
{
    return hudready && ingame && viewsize == 21;
}

// In any view size: with a smaller view the status bar is on the top screen
// and the map fills the bottom screen.
boolean N3DS_MapVisible (void)
{
    return showmap && ingame && player;
}

void N3DS_ResetMap (void)
{
    memset (seen, 0, sizeof (seen));
}

void N3DS_DrawHud (void)
{
    if (viewsize != 21)
        return;

    if (!hud)
    {
        hud = SDL_CreateRGBSurface (SDL_SWSURFACE, screenWidth, screenHeight, 8, 0, 0, 0, 0);
        if (!hud)
            return;
        SDL_SetColors (hud, gamepal, 0, 256);
    }

    // Draw with the normal status bar code into the HUD surface. The Draw*
    // functions return early when viewsize is 21, so pretend it is 20.
    SDL_Surface *savedsurface = curSurface;
    unsigned savedpitch = curPitch;
    int savedviewsize = viewsize;

    curSurface = hud;
    curPitch = hud->pitch;
    viewsize = 20;

    VWB_DrawPicScaledCoord ((screenWidth-scaleFactor*320)/2,
        screenHeight-scaleFactor*STATUSLINES, STATUSBARPIC);
    DrawFace ();
    DrawHealth ();
    DrawLives ();
    DrawLevel ();
    DrawAmmo ();
    DrawKeys ();
    DrawWeapon ();
    DrawScore ();

    viewsize = savedviewsize;
    curSurface = savedsurface;
    curPitch = savedpitch;
    hudready = true;
}

// ==========================================================================
// Drawing on the bottom half of the 400x480 surface (32 bit). The bottom
// screen shows its centre 320 columns.

static SDL_Surface *target;
static int targetx, targety;            // bottom screen origin on the surface
static int cliptop, clipbottom;         // rows of the bottom screen to draw on

static void Fill (int x, int y, int w, int h, Uint32 color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < cliptop ? cliptop : y;
    int x1 = x + w > 320 ? 320 : x + w;
    int y1 = y + h > clipbottom ? clipbottom : y + h;

    for (int j = y0; j < y1; j++)
    {
        Uint32 *row = (Uint32 *) ((Uint8 *) target->pixels + (targety + j) * target->pitch) + targetx;
        for (int i = x0; i < x1; i++)
            row[i] = color;
    }
}

// 3x5 digits, as in the n3ds-ports hello world.
static void DrawNumber (int number, int x, int y, int scale, Uint32 color)
{
    static const byte digits[10][5] =
    {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
        {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    };

    char text[12];
    snprintf (text, sizeof (text), "%d", number);

    for (int i = 0; text[i]; i++)
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (digits[text[i] - '0'][row] & (4 >> col))
                    Fill (x + (i * 4 + col) * scale, y + row * scale, scale, scale, color);
}

static boolean IsSeen (int x, int y)
{
    return x >= 0 && x < MAPSIZE && y >= 0 && y < MAPSIZE && seen[x][y];
}

// The zoom buttons, in the map's lower right corner: where they were drawn
// this frame (-1: not drawn), and whether the touch held is theirs.
static const int zoomsizes[NUMMAPZOOMS] = { 2, 3, 4, 6, 8 };   // pixels per tile
static const int zoombuttonw = 22, zoombuttonh = 18;
static const int zoomminusx = 320 - 2 * zoombuttonw - 6, zoomplusx = 320 - zoombuttonw - 2;
static int zoomy = -1;
static bool zoomowntouch, zoomwastouching;

// Mini-fuente 5x5 para dibujar texto limpio en la leyenda sin dependencias
static void DrawMiniChar (int x, int y, char c, Uint32 color)
{
    static const unsigned char font5x5[][5] = {
        {0x0E,0x11,0x1F,0x11,0x11}, // A
        {0x1E,0x11,0x1E,0x11,0x1E}, // B
        {0x0F,0x10,0x10,0x10,0x0F}, // C
        {0x1E,0x11,0x11,0x11,0x1E}, // D
        {0x1F,0x10,0x1E,0x10,0x1F}, // E
        {0x1F,0x10,0x1E,0x10,0x10}, // F
        {0x0F,0x10,0x13,0x11,0x0F}, // G
        {0x11,0x11,0x1F,0x11,0x11}, // H
        {0x0E,0x04,0x04,0x04,0x0E}, // I
        {0x01,0x01,0x01,0x11,0x0E}, // J
        {0x11,0x12,0x1C,0x12,0x11}, // K
        {0x10,0x10,0x10,0x10,0x1F}, // L
        {0x11,0x1B,0x15,0x11,0x11}, // M
        {0x11,0x19,0x15,0x13,0x11}, // N
        {0x0E,0x11,0x11,0x11,0x0E}, // O
        {0x1E,0x11,0x1E,0x10,0x10}, // P
        {0x0E,0x11,0x15,0x12,0x0D}, // Q
        {0x1E,0x11,0x1E,0x12,0x11}, // R
        {0x0F,0x10,0x0E,0x01,0x1E}, // S
        {0x1F,0x04,0x04,0x04,0x04}, // T
        {0x11,0x11,0x11,0x11,0x0E}, // U
        {0x11,0x11,0x11,0x0A,0x04}, // V
        {0x11,0x11,0x15,0x1B,0x11}, // W
        {0x11,0x0A,0x04,0x0A,0x11}, // X
        {0x11,0x0A,0x04,0x04,0x04}, // Y
        {0x1F,0x02,0x04,0x08,0x1F}  // Z
    };

    if (c >= 'A' && c <= 'Z')
    {
        const unsigned char *glyph = font5x5[c - 'A'];
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 5; col++)
                if (glyph[row] & (0x10 >> col))
                    Fill (x + col, y + row, 1, 1, color);
    }
    else if (c == ':')
    {
        Fill (x + 2, y + 1, 1, 1, color);
        Fill (x + 2, y + 3, 1, 1, color);
    }
    else if (c == '+')
    {
        Fill (x + 2, y + 1, 1, 3, color);
        Fill (x + 1, y + 2, 3, 1, color);
    }
    else if (c == '/')
    {
        for (int i = 0; i < 5; i++)
            Fill (x + 4 - i, y + i, 1, 1, color);
    }
    else if (c == '?')
    {
        Fill (x + 1, y, 3, 1, color);
        Fill (x + 4, y + 1, 1, 1, color);
        Fill (x + 2, y + 2, 2, 1, color);
        Fill (x + 2, y + 4, 1, 1, color);
    }
}

static void DrawMiniText (int x, int y, const char *str, Uint32 color)
{
    for (int i = 0; str[i]; i++)
        DrawMiniChar (x + i * 6, y, str[i], color);
}

static void DrawLegendOverlay (int maptop, int mapbottom)
{
    SDL_PixelFormat *fmt = target->format;
    Uint32 bg      = SDL_MapRGB (fmt, 0x10, 0x12, 0x1c);
    Uint32 border  = SDL_MapRGB (fmt, 0x70, 0x80, 0xa0);
    Uint32 title   = SDL_MapRGB (fmt, 0xff, 0xe6, 0x00);
    Uint32 txt     = SDL_MapRGB (fmt, 0xe8, 0xe8, 0xf0);
    Uint32 codecol = SDL_MapRGB (fmt, 0x00, 0xd0, 0xff);

    int w = 136, h = 64;
    int x0 = (320 - w) / 2;
    int y0 = maptop + (mapbottom - maptop - h) / 2;
    if (y0 < maptop + 2) y0 = maptop + 2;

    Fill (x0, y0, w, h, border);
    Fill (x0 + 1, y0 + 1, w - 2, h - 2, bg);

    DrawMiniText (x0 + 10, y0 + 6, "CHEATS (HOLD)", title);

    const char *codes[] = { "GOB", "VMP", "ENM", "ITM" };
    const char *descs[] = { ": GOD MODE", ": MAP/SECRETS", ": ENEMIES", ": ITEMS" };

    for (int i = 0; i < 4; i++)
    {
        int ry = y0 + 18 + i * 10;
        DrawMiniText (x0 + 10, ry, codes[i], codecol);
        DrawMiniText (x0 + 28, ry, descs[i], txt);
    }
}


static void DrawZoomButtons (int y)
{
    Uint32 frame = SDL_MapRGB (target->format, 0x60, 0x60, 0x70);
    Uint32 face = SDL_MapRGB (target->format, 0x18, 0x18, 0x24);
    Uint32 sign = SDL_MapRGB (target->format, 0xe0, 0xe0, 0xe0);
    Uint32 dim = SDL_MapRGB (target->format, 0x50, 0x50, 0x58);
    const int xs[2] = { zoomminusx, zoomplusx };

    for (int i = 0; i < 2; i++)
    {
        int x = xs[i];
        bool usable = i == 0 ? mapzoom > 0 : mapzoom < NUMMAPZOOMS - 1;

        Fill (x, y, zoombuttonw, zoombuttonh, frame);
        Fill (x + 1, y + 1, zoombuttonw - 2, zoombuttonh - 2, face);
        Fill (x + 6, y + zoombuttonh / 2 - 1, zoombuttonw - 12, 2, usable ? sign : dim);
        int helpx = zoomminusx - zoombuttonw - 4;
        Fill (helpx, y, zoombuttonw, zoombuttonh, frame);
        Fill (helpx + 1, y + 1, zoombuttonw - 2, zoombuttonh - 2, showlegend ? sign : face);
        DrawMiniChar (helpx + zoombuttonw / 2 - 2, y + zoombuttonh / 2 - 2, '?', showlegend ? face : sign);
        if (i == 1)
            Fill (x + zoombuttonw / 2 - 1, y + 4, 2, zoombuttonh - 8, usable ? sign : dim);
    }

    zoomy = y;
}
static double mapoffx = 0.0, mapoffy = 0.0;
static int lastmaptop = 0, lastmapbottom = 0;
static bool mappantouch = false, mappanned = false;
static int panlastx = 0, panlasty = 0;
bool N3DS_MapZoomUpdate (bool touching, int x, int y)
{
       if (touching && !zoomwastouching)
    {
        int helpx = zoomminusx - zoombuttonw - 4;
        bool helphit = zoomy >= 0 && y >= zoomy && y < zoomy + zoombuttonh
                       && x >= helpx && x < helpx + zoombuttonw;

        if (helphit || (showlegend && y >= lastmaptop && y < lastmapbottom))
        {
            showlegend = !showlegend;
            zoomowntouch = true;
        }
        else
        {
            zoomowntouch = zoomy >= 0 && y >= zoomy && y < zoomy + zoombuttonh
                           && ((x >= zoomminusx && x < zoomminusx + zoombuttonw)
                               || (x >= zoomplusx && x < zoomplusx + zoombuttonw));

            if (zoomowntouch)
            {
                if (x < zoomplusx && mapzoom > 0)
                    mapzoom--;
                else if (x >= zoomplusx && mapzoom < NUMMAPZOOMS - 1)
                    mapzoom++;
            }
            else if (y >= lastmaptop && y < lastmapbottom)
            {
                mappantouch = true;
                mappanned = false;
                panlastx = x;
                panlasty = y;
            }
        }
    }
    else if (touching && mappantouch)
    {
        int dx = x - panlastx;
        int dy = y - panlasty;

        // Si mueves el lápiz/dedo más de 3 píxeles, arrastra el mapa
        if (mappanned || abs (dx) > 3 || abs (dy) > 3)
        {
            const int tilesize = zoomsizes[mapzoom];
            mapoffx -= (double) dx / tilesize;
            mapoffy -= (double) dy / tilesize;
            panlastx = x;
            panlasty = y;
            mappanned = true;
        }
    }
    else if (!touching)
    {
        // Si diste un toque rápido sin arrastrar en el mapa, vuelve a centrarte en el jugador
        if (mappantouch && !mappanned)
        {
            mapoffx = 0.0;
            mapoffy = 0.0;
        }
        zoomowntouch = false;
        mappantouch = false;
        mappanned = false;
    }

    zoomwastouching = touching;
    return zoomowntouch || mappantouch;
}

bool N3DS_MapOwnsTouch (void)
{
    return zoomowntouch || mappantouch;
}

// Between maptop and mapbottom, centred on the player, at the zoom chosen.

static void DrawMap (int maptop, int mapbottom)
{
    lastmaptop = maptop;
    lastmapbottom = mapbottom;
    if (mapzoom < 0 || mapzoom >= NUMMAPZOOMS)
        mapzoom = 2;

    const int tilesize = zoomsizes[mapzoom];
    SDL_PixelFormat *format = target->format;
    Uint32 background = SDL_MapRGB (format, 0x08, 0x08, 0x10);
    Uint32 floorcolor = SDL_MapRGB (format, 0x28, 0x38, 0x50);
    Uint32 wallcolor = SDL_MapRGB (format, 0xa8, 0xa8, 0xb8);
    Uint32 doorcolor = SDL_MapRGB (format, 0xff, 0xe6, 0x00);
    Uint32 secretcolor = SDL_MapRGB (format, 0x00, 0xff, 0x80);
    Uint32 playercolor = SDL_MapRGB (format, 0xff, 0xff, 0xff);
    Uint32 enemycolor = SDL_MapRGB (format, 0xff, 0x00, 0x00);  // Rojo: enemigos vivos
    Uint32 itemcolor = SDL_MapRGB (format, 0x00, 0xd0, 0xff);   // Cian: ítems y llaves
    
    int enemysize = (tilesize / 2 > 3) ? (tilesize / 2) : 3;
    int itemsize = (tilesize / 2 > 2) ? (tilesize / 2) : 2;

    cliptop = maptop;
    clipbottom = mapbottom;
    Fill (0, maptop, 320, mapbottom - maptop, background);

    //double px = (double) player->x / TILEGLOBAL;
    //double py = (double) player->y / TILEGLOBAL;
    double px = (double) player->x / TILEGLOBAL + mapoffx;
    double py = (double) player->y / TILEGLOBAL + mapoffy;
    double centerx = 160;
    double centery = (maptop + mapbottom) / 2.0;

    for (int x = 0; x < MAPSIZE; x++)
    {
        for (int y = 0; y < MAPSIZE; y++)
        {
            Uint32 color = floorcolor;
            unsigned tile = tilemap[x][y];

            if (!tile)
            {
                if (!seen[x][y])
                    continue;
            }
            else
            {
                // Walls and doors appear once a tile next to them was seen.
                if (!IsSeen (x, y) && !IsSeen (x - 1, y) && !IsSeen (x + 1, y)
                        && !IsSeen (x, y - 1) && !IsSeen (x, y + 1))
                    continue;
                if (MAPSPOT (x, y, 1) == 98)
                    color = secretcolor;
                else{
                    color = (tile & 0x80) ? doorcolor : wallcolor;
                    }
            }

            Fill ((int) floor (centerx + (x - px) * tilesize),
                  (int) floor (centery + (y - py) * tilesize),
                  tilesize, tilesize, color);
        }
    }
         // 1. Dibujar ítems por tipo
        if (showitems){
             for (statobj_t *statptr = statobjlist; statptr != laststatobj; statptr++){
                if (statptr->shapenum == -1 || !(statptr->flags & FL_BONUS))
                    continue;

            Fill ((int) floor (centerx + (statptr->tilex - px + 0.3) * tilesize),
                  (int) floor (centery + (statptr->tiley - py + 0.3) * tilesize),
                  itemsize, itemsize, itemcolor);
        }
        }
        // 2. Dibujar enemigos vivos
         if (showenemies){
             
            for (objtype *ob = player->next; ob; ob = ob->next)
            {
                if (ob->flags & FL_SHOOTABLE)
                {
                    double ex = (double) ob->x / TILEGLOBAL;
                    double ey = (double) ob->y / TILEGLOBAL;
                    Fill ((int) floor (centerx + (ex - px - 0.25) * tilesize),
                          (int) floor (centery + (ey - py - 0.25) * tilesize),
                          enemysize, enemysize, enemycolor);
                }
        }
    }
    // The player: a dot and a line toward where they look (angle in
    // degrees, 0 is east, counterclockwise).
    double angle = player->angle * M_PI / 180;
    double dx = cos (angle), dy = -sin (angle);
    double playercx = centerx - mapoffx * tilesize;
    double playercy = centery - mapoffy * tilesize;

    for (int i = 0; i <= 2 * tilesize; i++)
        Fill ((int) (playercx + dx * i), (int) (playercy + dy * i), 1, 1, playercolor);
    Fill ((int) playercx - 1, (int) playercy - 1, 3, 3, playercolor);

    DrawZoomButtons (mapbottom - zoombuttonh - 2);
     if (showlegend)
        DrawLegendOverlay (maptop, mapbottom);
}

// The keyboard on the bottom screen draws with this.
static void KeyboardFill (int x, int y, int w, int h, unsigned rgb)
{
    Fill (x, y, w, h, SDL_MapRGB (target->format, rgb >> 16, (rgb >> 8) & 0xff, rgb & 0xff));
}

// ==========================================================================
// Frame counter: small, in a corner of the bottom screen.

static int fpsframes = 0;
static uint32_t fpstime = 0;
static int fps = 0, framems = 0;

static void CountFrame (void)
{
    fpsframes++;
    uint32_t now = SDL_GetTicks ();
    uint32_t elapsed = now - fpstime;
    if (elapsed < 1000)
        return;
    fps = fpsframes * 1000 / elapsed;
    framems = elapsed / fpsframes;
    fpsframes = 0;
    fpstime = now;
}

static void UpdateBottom (SDL_Surface *dest)
{
    static int laststate = -1;
    boolean hudvisible = N3DS_HudVisible ();
    boolean mapvisible = N3DS_MapVisible ();
    // The state includes the layout so a change clears the old one.
    boolean keyboard = n3ds_keyboard::IsOpen ();
    // The weapon buttons, in a game: the bottom rows, the rest as before.
    boolean weapons = weaponbar && ingame && player && !keyboard;
    int bottom = weapons ? 240 - n3ds_weapons::height : 240;
    int state = (hudvisible ? 1 + hudpos : 0) + 4 * mapvisible + 16 * showfps
              + 32 * keyboard + 64 * weapons;

    if (dest->h < N3DS_VIDEO_HEIGHT)
        return;

    target = dest;
    targetx = (dest->w - 320) / 2;
    zoomy = -1;                     // until the map draws them
    targety = screenHeight;

    if (state != laststate)         // clear the bottom screen when it changes
    {
        SDL_Rect all = { 0, (Sint16) screenHeight, (Uint16) dest->w, (Uint16) screenHeight };
        SDL_FillRect (dest, &all, SDL_MapRGB (dest->format, 0, 0, 0));
        laststate = state;
        if (hudvisible || mapvisible)
            N3DS_BottomScreenOn ();     // the HUD and the map are never hidden
    }

    if (SDL_MUSTLOCK (dest) && SDL_LockSurface (dest) < 0)
        return;

    if (!weapons)
        n3ds_weapons::Hide ();

    // The open keyboard covers the whole bottom screen.
    if (keyboard)
    {
        cliptop = 0;
        clipbottom = 240;
        n3ds_keyboard::Draw (KeyboardFill, 0);
        if (SDL_MUSTLOCK (dest))
            SDL_UnlockSurface (dest);
        return;
    }

    int statusrows = scaleFactor * STATUSLINES;
    int statusy = 0;

    // What the player sees is remembered even with the map hidden.
    if (ingame && player)
    {
        for (int x = 0; x < MAPSIZE; x++)
            for (int y = 0; y < MAPSIZE; y++)
                if (spotvis[x][y])
                    seen[x][y] = 1;
    }

    if (hudvisible)
    {
        int freerows = bottom - statusrows;

        if (mapvisible)
        {
            // Stats above or below the map, which fills the rest.
            statusy = hudpos == hudpos_bottom ? freerows : 0;

            if (statusy == 0)
                DrawMap (statusrows, bottom);
            else
                DrawMap (0, freerows);
        }
        else
            statusy = hudpos == hudpos_bottom ? freerows : 0;
    }
    else if (mapvisible)
        DrawMap (0, bottom);

    if (weapons)
    {
        // The knife, the pistol, the machine gun and the chaingun: the
        // best one and those before it are held.
        cliptop = 0;
        clipbottom = 240;
        n3ds_weapons::Draw (KeyboardFill, bottom, 4, (2u << gamestate.bestweapon) - 1,
                            gamestate.weapon);
    }

    // The keyboard's tab in the left corner and the frame counter in the
    // right one, away from the stats.
    int cornery = hudvisible && statusy == 0 ? statusrows + 4 : 4;
    cliptop = 0;
    clipbottom = 240;
    n3ds_keyboard::Draw (KeyboardFill, cornery);

    if (showfps)
    {
        // Frames per second (white) and milliseconds per frame (green).
        int y = cornery;
        Fill (256, y, 62, 13, SDL_MapRGB (dest->format, 0, 0, 0));
        DrawNumber (fps, 259, y + 2, 2, SDL_MapRGB (dest->format, 0xff, 0xff, 0xff));
        DrawNumber (framems, 289, y + 2, 2, SDL_MapRGB (dest->format, 0x80, 0xff, 0x80));
    }

    if (SDL_MUSTLOCK (dest))
        SDL_UnlockSurface (dest);

    if (hudvisible)
    {
        SDL_Rect src = { (Sint16) ((screenWidth - scaleFactor*320) / 2),
                         (Sint16) (screenHeight - statusrows),
                         (Uint16) (scaleFactor*320), (Uint16) statusrows };
        SDL_Rect dst = { (Sint16) targetx, (Sint16) (screenHeight + statusy), 0, 0 };
        SDL_BlitSurface (hud, &src, dest, &dst);
    }
}

void N3DS_Flip (SDL_Surface *dest)
{
    CountFrame ();
    UpdateBottom (dest);

    if (N3DS_Wide ())
    {
        // The 3D view at 800 columns, if this frame is the one just drawn.
        N3DS_WidePresent (dest, screenBuffer, n3ds_wide_view_ready);
        n3ds_wide_view_ready = false;
    }
    else
    {
        SDL_Flip (dest);
    }

    // SDL presents from its own thread, so nothing paced the game to the
    // screen: the game ran at its own 70 Hz and the screen showed 60 of
    // those frames unevenly. Wait for the vertical blank here instead (not
    // when the HOME Menu closes the game: the screens are no longer ours).
    // At 800x240 only if none passed since the last frame: a frame of a
    // little over 16.7 ms waited a whole refresh more (about 40 FPS).
    if (aptShouldClose ())
        return;
    if (N3DS_Wide ())
        gspWaitForEvent (GSPGPU_EVENT_VBlank0, false);
    else
        gspWaitForVBlank ();
}
