/*
 * video-render-palntsc.c - PAL/NTSC CRT renderers (used for VIC/VICII/TED)
 *
 * Written by
 *  John Selck <graham@cruise.de>
 *  Dag Lem <resid@nimrod.no>
 *  Andreas Boose <viceteam@t-online.de>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdio.h>

#include "log.h"
#include "machine.h"
#include "render1x1.h"
#include "render1x1pal.h"
#include "render1x1ntsc.h"
#include "render1x2.h"
#include "render2x2.h"
#include "render2x2pal.h"
#include "render2x2palu.h"
#include "render2x2ntsc.h"
#include "renderscale2x.h"
#include "resources.h"
#include "types.h"
#include "video-render.h"
#include "video.h"


void video_render_pal_ntsc_main(video_render_config_t *config,
                           uint8_t *src, uint8_t *trg,
                           int width, int height, int xs, int ys, int xt,
                           int yt, int pitchs, int pitcht,
                           int crt_type,
                           unsigned int viewport_first_line, unsigned int viewport_last_line)
{
    video_render_color_tables_t *colortab;
    int doublescan, crtemulation, rendermode, scale2x;

    rendermode = config->rendermode;
    doublescan = config->doublescan;
    colortab = &config->color_tables;

    scale2x = (config->filter == VIDEO_FILTER_SCALE2X);
    crtemulation = (config->filter == VIDEO_FILTER_CRT);

    /*
    if (config->external_palette)
        crtemulation = 0;
    */

    if ((rendermode == VIDEO_RENDER_PAL_NTSC_1X1
         || rendermode == VIDEO_RENDER_PAL_NTSC_2X2)
        && config->video_resources.pal_scanlineshade <= 0) {
        doublescan = 0;
    }

    switch (rendermode) {
        case VIDEO_RENDER_NULL:
            break;

        case VIDEO_RENDER_PAL_NTSC_1X1:
            if (crtemulation) {
                switch (crt_type) {
                    case VIDEO_CRT_TYPE_NTSC:
                        render_32_1x1_ntsc(colortab, src, trg, width, height,
                                        xs, ys, xt, yt, pitchs, pitcht);
                        return;
                    default:
                        /* fall through */
                    case VIDEO_CRT_TYPE_PAL:
                        render_32_1x1_pal(colortab, src, trg, width, height,
                                        xs, ys, xt, yt, pitchs, pitcht, config);
                        return;
                }
            } else {
                render_32_1x1_04(colortab, src, trg, width, height,
                                 xs, ys, xt, yt, pitchs, pitcht);
                return;
            }
            return;
#ifndef __LIBRETRO__
        case VIDEO_RENDER_PAL_NTSC_2X2:
            if (crtemulation) {
                switch (crt_type) {
                    case VIDEO_CRT_TYPE_NTSC:
                        render_32_2x2_ntsc(colortab, src, trg, width, height,
                                           xs, ys, xt, yt, pitchs, pitcht,
                                           viewport_first_line, viewport_last_line, config);
                        return;
                    default:
                        /* fall through */
                    case VIDEO_CRT_TYPE_PAL:
                        if (config->video_resources.delaylinetype == 1) {
                            /* delay U only (1084 style) */
                            render_32_2x2_pal_u(colortab, src, trg, width, height,
                                                xs, ys, xt, yt, pitchs, pitcht,
                                                viewport_first_line, viewport_last_line, config);
                            return;
                        }
                        render_32_2x2_pal(colortab, src, trg, width, height,
                                          xs, ys, xt, yt, pitchs, pitcht,
                                          viewport_first_line, viewport_last_line, config);
                        return;
                }
            } else if (scale2x) {
                render_32_scale2x(colortab, src, trg, width, height,
                                  xs, ys, xt, yt, pitchs, pitcht);
                return;
            } else {
                render_32_2x2(colortab, src, trg, width, height,
                                 xs, ys, xt, yt, pitchs, pitcht, doublescan, config);
                return;
            }
#endif
    }
    log_debug(LOG_DEFAULT, "video_render_pal_ntsc_main unsupported rendermode (%d)\n", rendermode);
}
