
/*
 * c64dtvstubs.c - dummies for unneeded/unused functions
 *
 * Written by
 *  groepaz <groepaz@gmx.net>
 *  Marco van den Heuvel <blackystardust68@yahoo.com>
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

#include <stdlib.h>
#include <stdbool.h>

#include "cart/clockport.h"
#include "cartridge.h"
#include "datasette.h"
#include "ds1307.h"
#include "rtc-58321a.h"
#include "rsuser.h"
#include "c64parallel.h"
#include "c64cart.h"
#include "c64/cart/c64cartmem.h"
#include "machine.h"
#include "mididrv.h"
#include "pet/petpia.h"
#include "snapshot.h"
#include "tapecart.h"
#include "tapeport.h"
#ifdef HAVE_LIBCURL
#include "userport_wic64.h"
#endif


tapeport_desc_t *tapeport_get_valid_devices(int port, int sort)
{
    return NULL;
}

const char *tapeport_get_device_type_desc(int type)
{
    return NULL;
}

int tapeport_valid_port(int port)
{
    return 0;
}

int machine_autodetect_psid(const char *name)
{
    return -1;
}

int tapeport_device_register(int id, tapeport_device_t *device)
{
    return 0;
}

void tapeport_trigger_flux_change(unsigned int on, int port)
{
}

void tapeport_set_tape_sense(int sense, int port)
{
}

int tapecart_is_valid(const char *filename)
{
    return 0;   /* FALSE */
}

int tapecart_attach_tcrt(const char *filename, void *unused)
{
    return -1;
}

int tapecart_flush_tcrt(void)
{
    return -1;
}

void machine_set_tape_write_in(int port, int val)
{
}
void machine_set_tape_motor_in(int port, int val)
{
}
void machine_trigger_flux_change(int port, unsigned int on)
{
}
void machine_set_tape_sense(int port, int sense)
{
}

#ifdef WINDOWS_COMPILE
void mididrv_ui_reset_device_list(int device)
{
}

char *mididrv_ui_get_next_device_name(int device, int *id)
{
    return NULL;
}
#endif

/*******************************************************************************
    clockport
*******************************************************************************/

clockport_supported_devices_t clockport_supported_devices[] = { { 0, NULL } };

/*******************************************************************************
    cartridge
*******************************************************************************/

/* Expansion port signals. */
export_t export = { 0, 0, 0, 0};

static uint8_t romh_banks[1]; /* dummy */

uint8_t *ultimax_romh_phi1_ptr(uint16_t addr)
{
    return romh_banks;
}

uint8_t *ultimax_romh_phi2_ptr(uint16_t addr)
{
    return romh_banks;
}

int cartridge_attach_image(int type, const char *filename)
{
    return -1;
}

void cartridge_detach_image(int type)
{
}

int cartridge_save_image(int type, const char *filename)
{
    return -1;
}

int cartridge_save_secondary_image(int type, const char *filename)
{
    return -1;
}

int cartridge_flush_image(int type)
{
    return -1;
}

int cartridge_flush_secondary_image(int type)
{
    return -1;
}

int cartridge_can_save_image(int crtid)
{
    return 0;
}

int cartridge_can_flush_image(int crtid)
{
    return 0;
}

int cartridge_can_save_secondary_image(int crtid)
{
    return 0;
}

int cartridge_can_flush_secondary_image(int crtid)
{
    return 0;
}

int cartridge_enable(int crtid)
{
    return -1;
}

int cartridge_disable(int crtid)
{
    return -1;
}

int cartridge_type_enabled(int crtid)
{
    return 0;
}

void cartridge_set_default(void)
{
}

void cartridge_unset_default(void)
{
}

cartridge_info_t *cartridge_get_info_list(void)
{
    return NULL;
}

/* return cartridge type of main slot
   returns 0 (CARTRIDGE_CRT) if crt file */
int cartridge_get_id(int slot)
{
    return CARTRIDGE_NONE;
}

char *cartridge_get_filename_by_slot(int slot)
{
    return NULL;
}

char *cartridge_get_secondary_filename_by_slot(int slot)
{
    return NULL;
}

void cartridge_trigger_freeze(void)
{
}

/*******************************************************************************
    userport devices
*******************************************************************************/

bool pia1_get_diagnostic_pin(void)
{
    return false;
}
#ifndef __LIBRETRO__
int ds1307_write_snapshot(rtc_ds1307_t *context, snapshot_t *s)
{
    return -1;
}
int ds1307_read_snapshot(rtc_ds1307_t *context, snapshot_t *s)
{
    return -1;
}
rtc_ds1307_t *ds1307_init(char *device)
{
    return NULL;
}
void ds1307_destroy(rtc_ds1307_t *context, int save)
{
}
void ds1307_set_clk_line(rtc_ds1307_t *context, uint8_t data)
{
}
void ds1307_set_data_line(rtc_ds1307_t *context, uint8_t data)
{
}
uint8_t ds1307_read_data_line(rtc_ds1307_t *context)
{
    return 0;
}

int rtc58321a_read_snapshot(rtc_58321a_t *context, snapshot_t *s)
{
    return -1;
}
int rtc58321a_write_snapshot(rtc_58321a_t *context, snapshot_t *s)
{
    return -1;
}
rtc_58321a_t *rtc58321a_init(char *device)
{
    return NULL;
}
void rtc58321a_destroy(rtc_58321a_t *context, int save)
{
}
uint8_t rtc58321a_read(rtc_58321a_t *context)
{
    return 0;
}
void rtc58321a_write_address(rtc_58321a_t *context, uint8_t address)
{
}
void rtc58321a_write_data(rtc_58321a_t *context, uint8_t data)
{
}
void rtc58321a_stop_clock(rtc_58321a_t *context)
{
}
void rtc58321a_start_clock(rtc_58321a_t *context)
{
}

int rsuser_resources_init(void)
{
    return -1;
}
int rsuser_cmdline_options_init(void)
{
    return -1;
}
#endif
int parallel_cable_cpu_resources_init(void)
{
    return -1;
}
