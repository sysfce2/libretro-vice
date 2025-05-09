/*
 * drivecpu.c - 6502 processor emulation of CBM disk drives.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Andreas Boose <viceteam@t-online.de>
 *
 * Patches by
 *  Andre Fachat <a.fachat@physik.tu-chemnitz.de>
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
#include <string.h>

#include "6510core.h"
#include "alarm.h"
#include "debug.h"
#include "drive.h"
#include "drivecpu.h"
#include "drive-check.h"
#include "drivemem.h"
#include "drivetypes.h"
#include "interrupt.h"
#include "lib.h"
#include "log.h"
#include "machine-drive.h"
#include "machine.h"
#include "mainlock.h"
#include "mem.h"
#include "monitor.h"
#include "mos6510.h"
#include "rotation.h"
#include "snapshot.h"
#include "types.h"
#include "uiapi.h"


#define DRIVE_CPU

#ifdef __LIBRETRO__
#define ANE_LOG_LEVEL 0
#define LXA_LOG_LEVEL 0
#endif

/* Global clock counters.  */
CLOCK diskunit_clk[NUM_DISK_UNITS];

static void drivecpu_jam(diskunit_context_t *drv);

static void drivecpu_set_bank_base(void *context);

static interrupt_cpu_status_t *drivecpu_int_status_ptr[NUM_DISK_UNITS];

void drivecpu_setup_context(struct diskunit_context_s *drv, int i)
{
    monitor_interface_t *mi;
    drivecpu_context_t *cpu;

    if (i) {
        drv->cpu = lib_calloc(1, sizeof(drivecpu_context_t));
    }
    cpu = drv->cpu;

    if (i) {
        drv->cpud = lib_calloc(1, sizeof(drivecpud_context_t));
        drv->func = lib_malloc(sizeof(drivefunc_context_t));

        cpu->int_status = interrupt_cpu_status_new();
        interrupt_cpu_status_init(cpu->int_status, &(cpu->last_opcode_info));
    }
    drivecpu_int_status_ptr[drv->mynumber] = cpu->int_status;

    cpu->rmw_flag = 0;
    cpu->d_bank_limit = 0;
    cpu->d_bank_start = 0;
    cpu->pageone = NULL;
    if (i) {
        cpu->snap_module_name = lib_msprintf("DRIVECPU%u", drv->mynumber);
        cpu->identification_string = lib_msprintf("DRIVE#%u", drv->mynumber + 8);
        cpu->monitor_interface = monitor_interface_new();
    }
    mi = cpu->monitor_interface;
    mi->context = (void *)drv;
    mi->cpu_regs = &(cpu->cpu_regs);
    mi->cpu_R65C02_regs = NULL;
    mi->cpu_65816_regs = NULL;
    mi->dtv_cpu_regs = NULL;
    mi->z80_cpu_regs = NULL;
    mi->h6809_cpu_regs = NULL;
    mi->int_status = cpu->int_status;
    mi->clk = &(diskunit_clk[drv->mynumber]);
    mi->current_bank = 0;
    mi->mem_bank_list = NULL;
    mi->mem_bank_list_nos = NULL;
    mi->mem_bank_from_name = NULL;
    mi->get_line_cycle = NULL;

    mi->mem_bank_read = drivemem_bank_read;
    mi->mem_bank_peek = drivemem_bank_peek;
    mi->mem_bank_write = drivemem_bank_store;
    mi->mem_bank_poke = drivemem_bank_poke;

    mi->mem_ioreg_list_get = drivemem_ioreg_list_get;
    mi->toggle_watchpoints_func = drivemem_toggle_watchpoints;
    mi->set_bank_base = drivecpu_set_bank_base;
    cpu->monspace = monitor_diskspace_mem(drv->mynumber);

    if (i) {
        drv->cpu->alarm_context = alarm_context_new(drv->cpu->identification_string);
    }
}

/* ------------------------------------------------------------------------- */

#define LOAD(a)           (*drv->cpud->read_func_ptr[(a) >> 8])(drv, (uint16_t)(a))
#define LOAD_ZERO(a)      (*drv->cpud->read_func_ptr[0])(drv, (uint16_t)(a))
#define LOAD_ADDR(a)      (LOAD((a)) | (LOAD((a) + 1) << 8))
#define LOAD_ZERO_ADDR(a) (LOAD_ZERO((a)) | (LOAD_ZERO((a) + 1) << 8))
#define STORE(a, b)       (*drv->cpud->store_func_ptr[(a) >> 8])(drv, (uint16_t)(a), (uint8_t)(b))
#define STORE_ZERO(a, b)  (*drv->cpud->store_func_ptr[0])(drv, (uint16_t)(a), (uint8_t)(b))

#define LOAD_DUMMY(a)           (*drv->cpud->read_func_ptr_dummy[(a) >> 8])(drv, (uint16_t)(a))
#define LOAD_ZERO_DUMMY(a)      (*drv->cpud->read_func_ptr_dummy[0])(drv, (uint16_t)(a))
#define LOAD_ADDR_DUMMY(a)      (LOAD_DUMMY((a)) | (LOAD_DUMMY((a) + 1) << 8))
#define LOAD_ZERO_ADDR_DUMMY(a) (LOAD_ZERO_DUMMY((a)) | (LOAD_ZERO_DUMMY((a) + 1) << 8))
#define STORE_DUMMY(a, b)       (*drv->cpud->store_func_ptr_dummy[(a) >> 8])(drv, (uint16_t)(a), (uint8_t)(b))
#define STORE_ZERO_DUMMY(a, b)  (*drv->cpud->store_func_ptr_dummy[0])(drv, (uint16_t)(a), (uint8_t)(b))

#define JUMP(addr)                                                         \
    do {                                                                   \
        reg_pc = (unsigned int)(addr);                                     \
        if (reg_pc >= cpu->d_bank_limit || reg_pc < cpu->d_bank_start) {   \
            uint8_t *p = drv->cpud->read_base_tab_ptr[reg_pc >> 8];           \
            cpu->d_bank_base = p;                                          \
                                                                           \
            if (p != NULL) {                                               \
                uint32_t limits = drv->cpud->read_limit_tab_ptr[reg_pc >> 8]; \
                cpu->d_bank_limit = limits & 0xffff;                       \
                cpu->d_bank_start = limits >> 16;                          \
            } else {                                                       \
                cpu->d_bank_start = 0;                                     \
                cpu->d_bank_limit = 0;                                     \
            }                                                              \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------------------- */

static void cpu_reset(diskunit_context_t *drv)
{
    int preserve_monitor;

    preserve_monitor = drv->cpu->int_status->global_pending_int & IK_MONITOR;

    log_message(drv->log, "RESET.");
    ui_display_reset(drv->mynumber + DRIVE_UNIT_MIN, 0);

    interrupt_cpu_status_reset(drv->cpu->int_status);

    *(drv->clk_ptr) = 6;
    rotation_reset(drv->drives[0]);
    rotation_reset(drv->drives[1]);
    machine_drive_reset(drv);

    if (preserve_monitor) {
        interrupt_monitor_trap_on(drv->cpu->int_status);
    }
}

void drivecpu_reset_clk(diskunit_context_t *drv)
{
    drv->cpu->last_clk = maincpu_clk;
    drv->cpu->last_exc_cycles = 0;
    drv->cpu->stop_clk = 0;
}

/* called by drive_reset() (via machine_specific_reset()) */
void drivecpu_reset(diskunit_context_t *drv)
{
    int preserve_monitor;

    *(drv->clk_ptr) = 0;
    drivecpu_reset_clk(drv);

    preserve_monitor = drv->cpu->int_status->global_pending_int & IK_MONITOR;

    interrupt_cpu_status_reset(drv->cpu->int_status);

    if (preserve_monitor) {
        interrupt_monitor_trap_on(drv->cpu->int_status);
    }

    /* FIXME -- ugly, should be changed in interrupt.h */
    interrupt_trigger_reset(drv->cpu->int_status, *(drv->clk_ptr));
}

/* called by drive_cpu_trigger_reset() */
void drivecpu_trigger_reset(unsigned int dnr)
{
    interrupt_trigger_reset(drivecpu_int_status_ptr[dnr], diskunit_clk[dnr] + 1);
}

void drivecpu_set_overflow(diskunit_context_t *drv)
{
    drivecpu_context_t *cpu = drv->cpu;
    cpu->cpu_regs.p |= P_OVERFLOW;
}

void drivecpu_shutdown(diskunit_context_t *drv)
{
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    if (cpu->alarm_context != NULL) {
        alarm_context_destroy(cpu->alarm_context);
    }

    monitor_interface_destroy(cpu->monitor_interface);
    interrupt_cpu_status_destroy(cpu->int_status);

    lib_free(cpu->snap_module_name);
    lib_free(cpu->identification_string);

    machine_drive_shutdown(drv);

    lib_free(drv->func);
    lib_free(drv->cpud);
    lib_free(cpu);
}

/* TODO: check type is already set, and remove type from parameters */
void drivecpu_init(diskunit_context_t *drv, int type)
{
    drivemem_init(drv);
    drivecpu_reset(drv);
}

inline void drivecpu_wake_up(diskunit_context_t *drv)
{
    /* FIXME: this value could break some programs, or be way too high for
       others.  Maybe we should put it into a user-definable resource.  */
    if (maincpu_clk - drv->cpu->last_clk > 0xffffff
        && *(drv->clk_ptr) > 934639) {
        log_message(drv->log, "Skipping cycles.");
        drv->cpu->last_clk = maincpu_clk;
    }
}

inline void drivecpu_sleep(diskunit_context_t *drv)
{
    /* Currently does nothing.  But we might need this hook some day.  */
}

/* Handle a ROM trap. */
inline static uint32_t drive_trap_handler(diskunit_context_t *drv)
{
    if (MOS6510_REGS_GET_PC(&(drv->cpu->cpu_regs)) == (uint16_t)drv->trap) {
        MOS6510_REGS_SET_PC(&(drv->cpu->cpu_regs), drv->trapcont);
        if (drv->idling_method == DRIVE_IDLE_TRAP_IDLE) {
            CLOCK next_clk;

            next_clk = alarm_context_next_pending_clk(drv->cpu->alarm_context);

            if (next_clk > drv->cpu->stop_clk) {
                next_clk = drv->cpu->stop_clk;
            }

            *(drv->clk_ptr) = next_clk;
        }
        return 0;
    }
    return (uint32_t)-1;
}

static void drive_generic_dma(void)
{
    /* Generic DMA hosts can be implemented here.
       Not very likey for disk drives. */
}

/* -------------------------------------------------------------------------- */

/* Return nonzero if a pending NMI should be dispatched now.  This takes
   account for the internal delays of the 6510, but does not actually check
   the status of the NMI line.  */
inline static int interrupt_check_nmi_delay(interrupt_cpu_status_t *cs,
                                            CLOCK cpu_clk)
{
    CLOCK nmi_clk = cs->nmi_clk + INTERRUPT_DELAY;

    /* BRK (0x00) delays the NMI by one opcode.  */
    /* TODO DO_INTERRUPT sets last opcode to 0: can NMI occur right after IRQ? */
    if (OPINFO_NUMBER(*cs->last_opcode_info_ptr) == 0x00) {
        return 0;
    }

    /* Branch instructions delay IRQs and NMI by one cycle if branch
       is taken with no page boundary crossing.  */
    if (OPINFO_DELAYS_INTERRUPT(*cs->last_opcode_info_ptr)) {
        nmi_clk++;
    }

    if (cpu_clk >= nmi_clk) {
        return 1;
    }

    return 0;
}

/* Return nonzero if a pending IRQ should be dispatched now.  This takes
   account for the internal delays of the 6510, but does not actually check
   the status of the IRQ line.  */
inline static int interrupt_check_irq_delay(interrupt_cpu_status_t *cs,
                                            CLOCK cpu_clk)
{
    CLOCK irq_clk = cs->irq_clk + INTERRUPT_DELAY;

    /* Branch instructions delay IRQs and NMI by one cycle if branch
       is taken with no page boundary crossing.  */
    if (OPINFO_DELAYS_INTERRUPT(*cs->last_opcode_info_ptr)) {
        irq_clk++;
    }

    /* If an opcode changes the I flag from 1 to 0, the 6510 needs
       one more opcode before it triggers the IRQ routine.  */
    if (cpu_clk >= irq_clk) {
        if (!OPINFO_ENABLES_IRQ(*cs->last_opcode_info_ptr)) {
            return 1;
        } else {
            cs->global_pending_int |= IK_IRQPEND;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Execute up to the current main CPU clock value.  This automatically
   calculates the corresponding number of clock ticks in the drive.  */
void drivecpu_execute(diskunit_context_t *drv, CLOCK clk_value)
{
    CLOCK cycles;
    CLOCK tcycles;
    drivecpu_context_t *cpu;

#define reg_a   (cpu->cpu_regs.a)
#define reg_x   (cpu->cpu_regs.x)
#define reg_y   (cpu->cpu_regs.y)
#define reg_pc  (cpu->cpu_regs.pc)
#define reg_sp  (cpu->cpu_regs.sp)
#define reg_p   (cpu->cpu_regs.p)
#define flag_z  (cpu->cpu_regs.z)
#define flag_n  (cpu->cpu_regs.n)
#define ORIGIN_MEMSPACE  (drv->mynumber + e_disk8_space)

    cpu = drv->cpu;

    drivecpu_wake_up(drv);

    /* Calculate number of main CPU clocks to emulate */
    if (clk_value > cpu->last_clk) {
        cycles = clk_value - cpu->last_clk;
    } else {
        cycles = 0;
    }

    while (cycles != 0) {
        tcycles = cycles > 10000 ? 10000 : cycles;
        cycles -= tcycles;

        cpu->cycle_accum += drv->cpud->sync_factor * tcycles;
        cpu->stop_clk += cpu->cycle_accum >> 16;
        cpu->cycle_accum &= 0xffff;
    }

    /* Run drive CPU emulation until the stop_clk clock has been reached. */
    while (*drv->clk_ptr < cpu->stop_clk) {
/* Include the 6502/6510 CPU emulation core.  */
#define CPU_LOG_ID (drv->log)
/* #define ANE_LOG_LEVEL ane_log_level */
/* #define LXA_LOG_LEVEL lxa_log_level */
#define CPU_IS_JAMMED cpu->is_jammed

#define CLK (*(drv->clk_ptr))
#define RMW_FLAG (cpu->rmw_flag)
#define PAGE_ONE (cpu->pageone)
#define LAST_OPCODE_INFO (cpu->last_opcode_info)
#define LAST_OPCODE_ADDR (cpu->last_opcode_addr)
#define TRACEFLG (debug.drivecpu_traceflg[drv->mynumber])

#define CPU_INT_STATUS (cpu->int_status)

#define ALARM_CONTEXT (cpu->alarm_context)

#define JAM() drivecpu_jam(drv)

#define ROM_TRAP_ALLOWED() 1

#define ROM_TRAP_HANDLER() drive_trap_handler(drv)

#define CALLER (cpu->monspace)

#define DMA_FUNC drive_generic_dma()

#define DMA_ON_RESET

#define drivecpu_byte_ready_egde_clear()  \
    do {                                  \
        drv->drives[0]->byte_ready_edge = 0;  \
    } while (0)

#define drivecpu_rotate()                 \
    do {                                  \
        rotation_rotate_disk(drv->drives[0]); \
    } while (0)

#define drivecpu_byte_ready() (drv->drives[0]->byte_ready_edge)

#define cpu_reset() (cpu_reset)(drv)
#define bank_limit (cpu->d_bank_limit)
#define bank_start (cpu->d_bank_start)
#define bank_base (cpu->d_bank_base)

#include "6510core.c"
    }

    cpu->last_clk = clk_value;
    drivecpu_sleep(drv);
}


/* ------------------------------------------------------------------------- */

static void drivecpu_set_bank_base(void *context)
{
    diskunit_context_t *drv;
    drivecpu_context_t *cpu;

    drv = (diskunit_context_t *)context;
    cpu = drv->cpu;

    JUMP(reg_pc);
}

/* Inlining this fuction makes no sense and would only bloat the code.  */
static void drivecpu_jam(diskunit_context_t *drv)
{
    unsigned int tmp;
    char *dname = "  Drive";
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    switch (drv->type) {
        case DRIVE_TYPE_1540:
            dname = "  1540";
            break;
        case DRIVE_TYPE_1541:
            dname = "  1541";
            break;
        case DRIVE_TYPE_1541II:
            dname = "1541-II";
            break;
        case DRIVE_TYPE_1551:
            dname = "  1551";
            break;
        case DRIVE_TYPE_1570:
            dname = "  1570";
            break;
        case DRIVE_TYPE_1571:
            dname = "  1571";
            break;
        case DRIVE_TYPE_1571CR:
            dname = "  1571CR";
            break;
        case DRIVE_TYPE_1581:
            dname = "  1581";
            break;
        case DRIVE_TYPE_2031:
            dname = "  2031";
            break;
        case DRIVE_TYPE_1001:
            dname = "  1001";
            break;
        case DRIVE_TYPE_2040:
            dname = "  2040";
            break;
        case DRIVE_TYPE_3040:
            dname = "  3040";
            break;
        case DRIVE_TYPE_4040:
            dname = "  4040";
            break;
        case DRIVE_TYPE_8050:
            dname = "  8050";
            break;
        case DRIVE_TYPE_8250:
            dname = "  8250";
            break;
        case DRIVE_TYPE_9000:
            dname = "  D9090/60";
            break;
    }

    tmp = drive_jam(drv->mynumber, "%s (%u) CPU: JAM at $%04X  ", dname, drv->mynumber + 8, (unsigned int)reg_pc);
    switch (tmp) {
        case JAM_RESET_CPU:
            reg_pc = 0xeaa0;
            drivecpu_set_bank_base((void *)drv);
            machine_trigger_reset(MACHINE_RESET_MODE_RESET_CPU);
            break;
        case JAM_POWER_CYCLE:
            reg_pc = 0xeaa0;
            drivecpu_set_bank_base((void *)drv);
            machine_trigger_reset(MACHINE_RESET_MODE_POWER_CYCLE);
            break;
        case JAM_MONITOR:
            monitor_startup(drv->cpu->monspace);
            break;
        default:
            CLK++;
    }
}

/* ------------------------------------------------------------------------- */

#define SNAP_MAJOR 1
#define SNAP_MINOR 3

int drivecpu_snapshot_write_module(diskunit_context_t *drv, snapshot_t *s)
{
    snapshot_module_t *m;
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    m = snapshot_module_create(s, drv->cpu->snap_module_name,
                               ((uint8_t)(SNAP_MAJOR)), ((uint8_t)(SNAP_MINOR)));
    if (m == NULL) {
        return -1;
    }

    if (0
        || SMW_CLOCK(m, *(drv->clk_ptr)) < 0
        || SMW_B(m, (uint8_t)MOS6510_REGS_GET_A(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (uint8_t)MOS6510_REGS_GET_X(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (uint8_t)MOS6510_REGS_GET_Y(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (uint8_t)MOS6510_REGS_GET_SP(&(cpu->cpu_regs))) < 0
        || SMW_W(m, (uint16_t)MOS6510_REGS_GET_PC(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (uint16_t)MOS6510_REGS_GET_STATUS(&(cpu->cpu_regs))) < 0
        || SMW_DW(m, (uint32_t)(cpu->last_opcode_info)) < 0
        || SMW_CLOCK(m, cpu->last_clk) < 0
        || SMW_CLOCK(m, cpu->cycle_accum) < 0
        || SMW_CLOCK(m, cpu->last_exc_cycles) < 0
        || SMW_CLOCK(m, cpu->stop_clk) < 0
        || SMW_B(m, cpu->cpu_last_data) < 0
        ) {
        goto fail;
    }

    if (interrupt_write_snapshot(cpu->int_status, m) < 0) {
        goto fail;
    }

    if (drv->type == DRIVE_TYPE_1540
        || drv->type == DRIVE_TYPE_1541
        || drv->type == DRIVE_TYPE_1541II
        || drv->type == DRIVE_TYPE_1551
        || drv->type == DRIVE_TYPE_1570
        || drv->type == DRIVE_TYPE_1571
        || drv->type == DRIVE_TYPE_1571CR
        || drv->type == DRIVE_TYPE_2031) {
        if (SMW_BA(m, drv->drive_ram, 0x800) < 0) {
            goto fail;
        }
    }

    if (drv->type == DRIVE_TYPE_1581
        || drv->type == DRIVE_TYPE_2000
        || drv->type == DRIVE_TYPE_4000) {
        if (SMW_BA(m, drv->drive_ram, 0x2000) < 0) {
            goto fail;
        }
    }
    if (drive_check_old(drv->type)) {
        if (SMW_BA(m, drv->drive_ram, 0x1100) < 0) {
            goto fail;
        }
    }

    if (interrupt_write_new_snapshot(cpu->int_status, m) < 0) {
        goto fail;
    }

    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    return -1;
}

int drivecpu_snapshot_read_module(diskunit_context_t *drv, snapshot_t *s)
{
    uint8_t major, minor;
    snapshot_module_t *m;
    uint8_t a, x, y, sp, status;
    uint16_t pc;
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    m = snapshot_module_open(s, drv->cpu->snap_module_name, &major, &minor);
    if (m == NULL) {
        return -1;
    }

    /* Before we start make sure all devices are reset.  */
    drivecpu_reset(drv);

    /* XXX: Assumes `CLOCK' is the same size as a `DWORD'.  */
    if (0
        || SMR_CLOCK(m, drv->clk_ptr) < 0
        || SMR_B(m, &a) < 0
        || SMR_B(m, &x) < 0
        || SMR_B(m, &y) < 0
        || SMR_B(m, &sp) < 0
        || SMR_W(m, &pc) < 0
        || SMR_B(m, &status) < 0
        || SMR_DW_UINT(m, &(cpu->last_opcode_info)) < 0
        || SMR_CLOCK(m, &(cpu->last_clk)) < 0
        || SMR_CLOCK(m, &(cpu->cycle_accum)) < 0
        || SMR_CLOCK(m, &(cpu->last_exc_cycles)) < 0
        || SMR_CLOCK(m, &(cpu->stop_clk)) < 0
        || SMR_B(m, &(cpu->cpu_last_data)) < 0
        ) {
        goto fail;
    }

    MOS6510_REGS_SET_A(&(cpu->cpu_regs), a);
    MOS6510_REGS_SET_X(&(cpu->cpu_regs), x);
    MOS6510_REGS_SET_Y(&(cpu->cpu_regs), y);
    MOS6510_REGS_SET_SP(&(cpu->cpu_regs), sp);
    MOS6510_REGS_SET_PC(&(cpu->cpu_regs), pc);
    MOS6510_REGS_SET_STATUS(&(cpu->cpu_regs), status);

    log_message(drv->log, "RESET (For undump).");

    interrupt_cpu_status_reset(cpu->int_status);

    machine_drive_reset(drv);

    if (interrupt_read_snapshot(cpu->int_status, m) < 0) {
        goto fail;
    }

    if (drv->type == DRIVE_TYPE_1540
        || drv->type == DRIVE_TYPE_1541
        || drv->type == DRIVE_TYPE_1541II
        || drv->type == DRIVE_TYPE_1551
        || drv->type == DRIVE_TYPE_1570
        || drv->type == DRIVE_TYPE_1571
        || drv->type == DRIVE_TYPE_1571CR
        || drv->type == DRIVE_TYPE_2031) {
        if (SMR_BA(m, drv->drive_ram, 0x800) < 0) {
            goto fail;
        }
    }

    if (drv->type == DRIVE_TYPE_1581
        || drv->type == DRIVE_TYPE_2000
        || drv->type == DRIVE_TYPE_4000) {
        if (SMR_BA(m, drv->drive_ram, 0x2000) < 0) {
            goto fail;
        }
    }

    if (drive_check_old(drv->type)) {
        if (SMR_BA(m, drv->drive_ram, 0x1100) < 0) {
            goto fail;
        }
    }

    /* Update `*bank_base'.  */
    JUMP(reg_pc);

    if (interrupt_read_new_snapshot(drv->cpu->int_status, m) < 0) {
        goto fail;
    }

    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    return -1;
}
