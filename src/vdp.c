#include "config.h"
#include "types.h"

#include "vdp.h"

#include "vdp_dma.h"
#include "vdp_tile.h"
#include "vdp_pal.h"
#include "vdp_spr.h"
#include "vdp_bg.h"

#include "tools.h"
#include "string.h"
#include "memory.h"
#include "dma.h"
#include "timer.h"
#include "sys.h"

#include "font.h"


#define WINDOW_DEFAULT          0xD000      // multiple of 0x1000 (0x0800 in H32)
#define HSCRL_DEFAULT           0xF000      // multiple of 0x0400
#define SLIST_DEFAULT           0xF400      // multiple of 0x0400 (0x0200 in H32)
#define APLAN_DEFAULT           0xE000      // multiple of 0x2000
#define BPLAN_DEFAULT           0xC000      // multiple of 0x2000


// we don't want to share it
extern void addFrameLoad(u16 frameLoad);

// forward
static void updateMapsAddress();
static void computeFrameCPULoad(u16 blank, u16 vcnt);
u16 getAdjustedVCounterInternal(u16 blank, u16 vcnt);


static u8 regValues[0x13];

u16 window_addr;
u16 aplan_addr;
u16 bplan_addr;
u16 hscrl_addr;
u16 slist_addr;
u16 maps_addr;

u16 screenWidth;
u16 screenHeight;
u16 planWidth;
u16 planHeight;
u16 windowWidth;
u16 planWidthSft;
u16 planHeightSft;
u16 windowWidthSft;


// constants for plan
const VDPPlan PLAN_A = { CONST_PLAN_A };
const VDPPlan PLAN_B = { CONST_PLAN_B };
const VDPPlan PLAN_WINDOW = { CONST_PLAN_WINDOW };


void VDP_init()
{
    vu16 *pw;
    u16 i;

    // wait for DMA completion
    VDP_waitDMACompletion();

    // default VRAM organization
    window_addr = WINDOW_DEFAULT;
    aplan_addr = APLAN_DEFAULT;
    bplan_addr = BPLAN_DEFAULT;
    slist_addr = SLIST_DEFAULT;
    hscrl_addr = HSCRL_DEFAULT;
    // get minimum address of all map/table (default is plan B)
    maps_addr = BPLAN_DEFAULT;

    // default resolution
    screenWidth = 320;
    screenHeight = 224;
    planWidth = 64;
    planHeight = 32;
    windowWidth = 64;
    planWidthSft = 6;
    planHeightSft = 5;
    windowWidthSft = 6;

    regValues[0x00] = 0x04;
    regValues[0x01] = 0x74;                     /* reg. 1 - Enable display, VBL, DMA + VCell size */
    regValues[0x02] = aplan_addr / 0x400;       /* reg. 2 - Plane A = $E000 */
    regValues[0x03] = window_addr / 0x400;      /* reg. 3 - Window  = $D000 */
    regValues[0x04] = bplan_addr / 0x2000;      /* reg. 4 - Plane B = $C000 */
    regValues[0x05] = slist_addr / 0x200;       /* reg. 5 - Sprite table = $F400 */
    regValues[0x06] = 0x00;                     /* reg. 6 - not used */
    regValues[0x07] = 0x00;                     /* reg. 7 - Background Color number*/
    regValues[0x08] = 0x00;                     /* reg. 8 - not used */
    regValues[0x09] = 0x00;                     /* reg. 9 - not used */
    regValues[0x0A] = 0x01;                     /* reg 10 - HInterrupt timing */
    regValues[0x0B] = 0x00;                     /* reg 11 - $0000abcd a=extr.int b=vscr cd=hscr */
    regValues[0x0C] = 0x81;                     /* reg 12 - hcell mode + shadow/highight + interlaced mode (40 cell, no shadow, no interlace) */
    regValues[0x0D] = hscrl_addr / 0x400;       /* reg 13 - HScroll Table = $F000 */
    regValues[0x0E] = 0x00;                     /* reg 14 - not used */
    regValues[0x0F] = 0x02;                     /* reg 15 - auto increment data */
    regValues[0x10] = 0x01;                     /* reg 16 - scrl screen v&h size (32x64) */
    regValues[0x11] = 0x00;                     /* reg 17 - window hpos */
    regValues[0x12] = 0x00;                     /* reg 18 - window vpos */

    // set registers
    pw = (u16 *) GFX_CTRL_PORT;
    for (i = 0x00; i < 0x13; i++) *pw = 0x8000 | (i << 8) | regValues[i];

    // these lines can be used in your code to change VRAM layout as olders SGDK (<= 1.30)
    /*
    VDP_setPlanSize(64, 64);

    VDP_setWindowAddress(0xB000);
    VDP_setSpriteListAddress(0xBC00);
    VDP_setHScrollTableAddress(0xB800);
    VDP_setBPlanAddress(0xC000);
    VDP_setAPlanAddress(0xE000);
    */

    // clear VRAM, reset palettes and scroll mode
    VDP_resetScreen();

    // load default font
    if (!VDP_loadFont(&font_default, 0))
    {
        // fatal error --> die here
//        PAL_setColors((PAL0 * 16) + (font_pal_lib.index & 0xF), font_pal_lib.data, font_pal_lib.length);
        // the font did not get loaded so maybe not really useful to show these messages...
        VDP_drawText("A fatal error occured !", 2, 2);
        VDP_drawText("cannot continue...", 4, 3);
        VDP_drawText("Not enough memory to reset VDP !", 0, 5);
        while(1);
    }

    // reset sprite struct
    VDP_resetSprites();

    // default plan and base tile attribut for draw text method
    VDP_setTextPlan(PLAN_A);
    VDP_setTextPalette(PAL0);
    VDP_setTextPriority(TRUE);

    // internal
    curTileInd = TILE_USERINDEX;
}


u8 VDP_getReg(u16 reg)
{
    if (reg < 0x13) return regValues[reg];
    else return 0;
}

void VDP_setReg(u16 reg, u8 value)
{
    vu16 *pw;
    u16 v;

    // update cached values
    switch (reg & 0x1F)
    {
        default:
            v = value;
            break;

        case 0x01:
            if (IS_PALSYSTEM)
            {
                v = value;
                if (v & 0x08) screenHeight = 240;
                else screenHeight = 224;
            }
            else
                v = value & 0xF7;
            break;

        case 0x02:
            v = value & 0x38;
            // update plan address
            aplan_addr = v * 0x400;
            updateMapsAddress();
            break;

        case 0x03:
            // 40H mode
            if (regValues[0x0C] & 0x81) v = value & 0x3C;
            // 32H mode
            else v = value & 0x3E;
            window_addr = v * 0x0400;
            updateMapsAddress();
            break;

        case 0x04:
            v = value & 0x7;
            // update text plan address
            bplan_addr = v * 0x2000;
            updateMapsAddress();
            break;

        case 0x05:
            // 40H mode
            if (regValues[0x0C] & 0x81) v = value & 0x7E;
            // 32H mode
            else v = value & 0x7F;
            slist_addr = v * 0x0200;
            updateMapsAddress();
            break;

        case 0x0C:
            v = value;
            if (v & 0x81)
            {
                screenWidth = 320;
                windowWidth = 64;
                windowWidthSft = 6;
            }
            else
            {
                screenWidth = 256;
                windowWidth = 32;
                windowWidthSft = 5;
            }
            break;

        case 0x0D:
            v = value & 0x3F;
            hscrl_addr = v * 0x0400;
            updateMapsAddress();
            break;

        case 0x10:
            v = value;
            if (v & 0x02)
            {
                planWidth = 128;
                planWidthSft = 7;
            }
            else if (v & 0x01)
            {
                planWidth = 64;
                planWidthSft = 6;
            }
            else
            {
                planWidth = 32;
                planWidthSft = 5;
            }
            if (v & 0x20)
            {
                planHeight = 128;
                planHeightSft = 7;
            }
            else if (v & 0x10)
            {
                planHeight = 64;
                planHeightSft = 6;
            }
            else
            {
                planHeight = 32;
                planHeightSft = 5;
            }
            break;
    }

    if (reg < 0x13) regValues[reg] = v;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8000 | (reg << 8) | v;
}

u8 VDP_getEnable()
{
    return regValues[0x01] & 0x40;
}

void VDP_setEnable(u8 value)
{
    vu16 *pw;

    if (value) regValues[0x01] |= 0x40;
    else regValues[0x01] &= ~0x40;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8100 | regValues[0x01];
}


u16 VDP_getScanlineNumber()
{
    if IS_PALSYSTEM return 312;
    else return 262;
}

u16 VDP_getScreenHeight()
{
    return screenHeight;
}

void VDP_setScreenHeight224()
{
    vu16 *pw;

    regValues[0x01] &= ~0x08;
    screenHeight = 224;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8100 | regValues[0x01];
}

void VDP_setScreenHeight240()
{
    vu16 *pw;

    if (IS_PALSYSTEM)
    {
        regValues[0x01] |= 0x08;
        screenHeight = 240;

        pw = (u16 *) GFX_CTRL_PORT;
        *pw = 0x8100 | regValues[0x01];
    }
}

u16 VDP_getScreenWidth()
{
    return screenWidth;
}

void VDP_setScreenWidth256()
{
    vu16 *pw;

    regValues[0x0C] &= ~0x81;
    screenWidth = 256;
    windowWidth = 32;
    windowWidthSft = 5;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8C00 | regValues[0x0C];
}

void VDP_setScreenWidth320()
{
    vu16 *pw;

    regValues[0x0C] |= 0x81;
    screenWidth = 320;
    windowWidth = 64;
    windowWidthSft = 6;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8C00 | regValues[0x0C];
}


u16 VDP_getPlanWidth()
{
    return planWidth;
}

u16 VDP_getPlanHeight()
{
    return planHeight;
}

void VDP_setPlanSize(u16 w, u16 h)
{
    vu16 *pw;
    u16 v = 0;

    if (w & 0x80)
    {
        planWidth = 128;
        planWidthSft = 7;
        v |= 0x03;
    }
    else if (w & 0x40)
    {
        planWidth = 64;
        planWidthSft = 6;
        v |= 0x01;
    }
    else
    {
        planWidth = 32;
        planWidthSft = 5;
    }
    if (h & 0x80)
    {
        planHeight = 128;
        planHeightSft = 7;
        v |= 0x30;
    }
    else if (h & 0x40)
    {
        planHeight = 64;
        planHeightSft = 6;
        v |= 0x10;
    }
    else
    {
        planHeight = 32;
        planHeightSft = 5;
    }

    regValues[0x10] = v;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x9000 | regValues[0x10];
}


u8 VDP_getVerticalScrollingMode()
{
    return (regValues[0x0B] >> 2) & 1;
}

u8 VDP_getHorizontalScrollingMode()
{
    return regValues[0x0B] & 3;
}

void VDP_setScrollingMode(u16 hscroll, u16 vscroll)
{
    vu16 *pw;

    regValues[0x0B] = ((vscroll & 1) << 2) | (hscroll & 3);

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8B00 | regValues[0x0B];
}


u8 VDP_getBackgroundColor()
{
    return regValues[0x07];
}

void VDP_setBackgroundColor(u8 value)
{
    vu16 *pw;

    regValues[0x07] = value & 0x3F;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8700 | regValues[0x07];
}


u8 VDP_getAutoInc()
{
    return regValues[0x0F];
}

void VDP_setAutoInc(u8 value)
{
    vu16 *pw;

    regValues[0x0F] = value;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8F00 | value;
}


u8 VDP_getDMAEnabled()
{
    return regValues[0x01] & 0x10;
}

void VDP_setDMAEnabled(u8 value)
{
    vu16 *pw;

    if (value) regValues[0x01] |= 0x10;
    else regValues[0x01] &= ~0x10;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8100 | regValues[0x01];
}

u8 VDP_getHVLatching()
{
    return regValues[0x00] & 0x02;
}

void VDP_setHVLatching(u8 value)
{
    vu16 *pw;

    if (value) regValues[0x00] |= 0x02;
    else regValues[0x00] &= ~0x02;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8000 | regValues[0x00];
}

void VDP_setHInterrupt(u8 value)
{
    vu16 *pw;

    if (value) regValues[0x00] |= 0x10;
    else regValues[0x00] &= ~0x10;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8000 | regValues[0x00];
}

void VDP_setHilightShadow(u8 value)
{
    vu16 *pw;

    if (value) regValues[0x0C] |= 0x08;
    else regValues[0x0C] &= ~0x08;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8C00 | regValues[0x0C];
}


u8 VDP_getHIntCounter()
{
    return regValues[0x0A];
}

void VDP_setHIntCounter(u8 value)
{
    vu16 *pw;

    regValues[0x0A] = value;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8A00 | regValues[0x0A];
}


u16 VDP_getAPlanAddress()
{
    return aplan_addr;
}

u16 VDP_getBPlanAddress()
{
    return bplan_addr;
}

u16 VDP_getWindowAddress()
{
    return window_addr;
}

u16 VDP_getWindowPlanAddress()
{
    return VDP_getWindowAddress();
}

u16 VDP_getSpriteListAddress()
{
    return slist_addr;
}

u16 VDP_getHScrollTableAddress()
{
    return hscrl_addr;
}


void VDP_setAPlanAddress(u16 value)
{
    vu16 *pw;

    aplan_addr = value & 0xE000;
    updateMapsAddress();

    regValues[0x02] = aplan_addr / 0x400;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8200 | regValues[0x02];
}

void VDP_setWindowAddress(u16 value)
{
    vu16 *pw;

    // 40H mode
    if (regValues[0x0C] & 0x81) window_addr = value & 0xF000;
    // 32H mode
    else window_addr = value & 0xF800;
    updateMapsAddress();

    regValues[0x03] = window_addr / 0x400;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8300 | regValues[0x03];
}

void VDP_setWindowPlanAddress(u16 value)
{
    VDP_setWindowAddress(value);
}

void VDP_setBPlanAddress(u16 value)
{
    vu16 *pw;

    bplan_addr = value & 0xE000;
    updateMapsAddress();

    regValues[0x04] = bplan_addr / 0x2000;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8400 | regValues[0x04];
}

void VDP_setSpriteListAddress(u16 value)
{
    vu16 *pw;

    // 40H mode
    if (regValues[0x0C] & 0x81) slist_addr = value & 0xFC00;
    // 32H mode
    else slist_addr = value & 0xFE00;
    updateMapsAddress();

    regValues[0x05] = slist_addr / 0x200;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8500 | regValues[0x05];
}

void VDP_setHScrollTableAddress(u16 value)
{
    vu16 *pw;

    hscrl_addr = value & 0xFC00;
    updateMapsAddress();

    regValues[0x0D] = hscrl_addr / 0x400;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8D00 | regValues[0x0D];
}

void VDP_setScanMode(u16 value)
{
    vu16 *pw;

    if (value == 0)
        // non-interlaced
        regValues[0x0C] &= ~0x06;
    else if (value == 1)
        // interlace mode 1
        regValues[0x0C] = (regValues[0x0C] & ~0x04) | 0x02;
    else
        // interlace mode 2
        regValues[0x0C] |= 0x06;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x8C00 | regValues[0x0C];
}

void VDP_setWindowHPos(u16 right, u16 pos)
{
    vu16 *pw;
    u16 v;

    v = pos & 0x7F;
    if (right) v |= 0x80;

    regValues[0x11] = v;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x9100 | v;
}

void VDP_setWindowVPos(u16 down, u16 pos)
{
    vu16 *pw;
    u16 v;

    v = pos & 0x7F;
    if (down) v |= 0x80;

    regValues[0x12] = v;

    pw = (u16 *) GFX_CTRL_PORT;
    *pw = 0x9200 | v;
}


void VDP_waitDMACompletion()
{
    while(GET_VDPSTATUS(VDP_DMABUSY_FLAG));
}

void VDP_waitFIFOEmpty()
{
    while(!GET_VDPSTATUS(VDP_FIFOEMPTY_FLAG));
}


void VDP_waitVSync()
{
    vu16 *pw = (u16 *) GFX_CTRL_PORT;

    // store V-Counter and initial blank state
    const u16 vcnt = GET_VCOUNTER;
    const u16 blank = *pw & VDP_VBLANK_FLAG;

    while (*pw & VDP_VBLANK_FLAG);
    while (!(*pw & VDP_VBLANK_FLAG));

    computeFrameCPULoad(blank, vcnt);
}

void VDP_waitVInt()
{
    // in VInt --> return
    if (SYS_isInVIntCallback()) return;

    // initial frame counter
    const u32 t = vtimer;
    // store V-Counter and initial blank state
    const u16 vcnt = GET_VCOUNTER;
    const u16 blank = GET_VDPSTATUS(VDP_VBLANK_FLAG);

    // wait for next VInt
    while (vtimer == t);

    computeFrameCPULoad(blank, vcnt);
}

static void computeFrameCPULoad(u16 blank, u16 vcnt)
{
    // update CPU frame load
    addFrameLoad(getAdjustedVCounterInternal(blank, vcnt));
}


void VDP_resetScreen()
{
    u16 i;

    // reset video memory (len = 0 is a special value to define 0x10000)
    DMA_doVRamFill(0, 0, 0, 1);
    // wait for DMA completion
    VDP_waitDMACompletion();

     // system tiles (16 plain tile)
    i = 16;
    while(i--) VDP_fillTileData(i | (i << 4), TILE_SYSTEMINDEX + i, 1, TRUE);

    PAL_setPalette(PAL0, palette_grey);
    PAL_setPalette(PAL1, palette_red);
    PAL_setPalette(PAL2, palette_green);
    PAL_setPalette(PAL3, palette_blue);

    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(PLAN_A, 0);
    VDP_setHorizontalScroll(PLAN_B, 0);
    VDP_setVerticalScroll(PLAN_A, 0);
    VDP_setVerticalScroll(PLAN_B, 0);
}

u16 getAdjustedVCounterInternal(u16 blank, u16 vcnt)
{
    u16 result = vcnt;

    // adjust V-Counter to take care of blanking rollback
    if (IS_PALSYSTEM)
    {
        // blank adjustement
        if (blank && ((result >= 0xCA) || (result <= 0x0A))) result = 8;
        else result += 16;
    }
    else
    {
        // blank adjustement
        if (blank && (result >= 0xDF)) result = 16;
        else result += 32;
    }

    return result;
}

u16 VDP_getAdjustedVCounter()
{
    return getAdjustedVCounterInternal(GET_VDPSTATUS(VDP_VBLANK_FLAG), GET_VCOUNTER);
}


void VDP_showFPS(u16 asFloat)
{
    char str[16];

    if (asFloat)
    {
        fix32ToStr(SYS_getFPSAsFloat(), str, 1);
        VDP_clearText(2, 1, 5);
    }
    else
    {
        uintToStr(SYS_getFPS(), str, 1);
        VDP_clearText(2, 1, 2);
    }

    // display FPS
    VDP_drawText(str, 1, 1);
}

void VDP_showCPULoad()
{
    char str[16];

    uintToStr(SYS_getCPULoad(), str, 1);
    strcat(str, "%");
    VDP_clearText(2, 2, 4);

    // display FPS
    VDP_drawText(str, 1, 2);
}


static void updateMapsAddress()
{
    u16 min_addr = window_addr;

    if (bplan_addr < min_addr) min_addr = bplan_addr;
    if (aplan_addr < min_addr) min_addr = aplan_addr;
    if (hscrl_addr < min_addr) min_addr = hscrl_addr;
    if (slist_addr < min_addr) min_addr = slist_addr;

    // need to reload font
    if (min_addr != maps_addr)
    {
        maps_addr = min_addr;
        // reload default font as its VRAM address has changed
        VDP_loadFont(&font_default, TRUE);
    }
}
