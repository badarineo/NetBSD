/* $NetBSD: pci_eb64plus.c,v 1.26 2021/06/19 16:59:07 thorpej Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/cdefs.h>			/* RCS ID & Copyright macro defns */

__KERNEL_RCSID(0, "$NetBSD: pci_eb64plus.c,v 1.26 2021/06/19 16:59:07 thorpej Exp $");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/syslog.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <alpha/pci/apecsreg.h>
#include <alpha/pci/apecsvar.h>

#include "sio.h"
#if NSIO
#include <alpha/pci/siovar.h>
#endif

#define	EB64PLUS_MAX_IRQ	32
#define	PCI_STRAY_MAX		5

static bus_space_tag_t eb64plus_intrgate_iot;
static bus_space_handle_t eb64plus_intrgate_ioh;

/* See pci_eb64plus_intr.s */
extern void	eb64plus_intr_enable(pci_chipset_tag_t, int irq);
extern void	eb64plus_intr_disable(pci_chipset_tag_t, int irq);

static void
pci_eb64plus_pickintr(void *core, bus_space_tag_t iot, bus_space_tag_t memt,
    pci_chipset_tag_t pc)
{
	char *cp;
	int i;

	pc->pc_intr_v = core;
	pc->pc_intr_map = alpha_pci_generic_intr_map;
	pc->pc_intr_string = alpha_pci_generic_intr_string;
	pc->pc_intr_evcnt = alpha_pci_generic_intr_evcnt;
	pc->pc_intr_establish = alpha_pci_generic_intr_establish;
	pc->pc_intr_disestablish = alpha_pci_generic_intr_disestablish;

	/* Not supported on the EB64+. */
	pc->pc_pciide_compat_intr_establish = NULL;

	eb64plus_intrgate_iot = iot;
	if (bus_space_map(eb64plus_intrgate_iot, 0x804, 3, 0,
	    &eb64plus_intrgate_ioh) != 0)
		panic("pci_eb64plus_pickintr: couldn't map interrupt PLD");
	for (i = 0; i < EB64PLUS_MAX_IRQ; i++)
		eb64plus_intr_disable(pc, i);	

#define PCI_EB64PLUS_IRQ_STR	8
	pc->pc_shared_intrs = alpha_shared_intr_alloc(EB64PLUS_MAX_IRQ,
	    PCI_EB64PLUS_IRQ_STR);
	pc->pc_intr_desc = "eb64+ irq";
	pc->pc_vecbase = 0x900;
	pc->pc_nirq = EB64PLUS_MAX_IRQ;

	pc->pc_intr_enable = eb64plus_intr_enable;
	pc->pc_intr_disable = eb64plus_intr_disable;

	for (i = 0; i < EB64PLUS_MAX_IRQ; i++) {
		alpha_shared_intr_set_maxstrays(pc->pc_shared_intrs, i,
			PCI_STRAY_MAX);
		
		cp = alpha_shared_intr_string(pc->pc_shared_intrs, i);
		snprintf(cp, PCI_EB64PLUS_IRQ_STR, "irq %d", i);
		evcnt_attach_dynamic(alpha_shared_intr_evcnt(
		    pc->pc_shared_intrs, i), EVCNT_TYPE_INTR, NULL,
		    "eb64+", cp);
	}

#if NSIO
	sio_intr_setup(pc, iot);
#endif
}
ALPHA_PCI_INTR_INIT(ST_EB64P, pci_eb64plus_pickintr)

#if 0		/* THIS DOES NOT WORK!  see pci_eb64plus_intr.S. */
uint8_t eb64plus_intr_mask[3] = { 0xff, 0xff, 0xff };

void
eb64plus_intr_enable(pci_chipset_tag_t pc __unused, int irq)
{
	int byte = (irq / 8), bit = (irq % 8);

#if 1
	printf("eb64plus_intr_enable: enabling %d (%d:%d)\n", irq, byte, bit);
#endif
	eb64plus_intr_mask[byte] &= ~(1 << bit);

	bus_space_write_1(eb64plus_intrgate_iot, eb64plus_intrgate_ioh, byte,
	    eb64plus_intr_mask[byte]);
}

void
eb64plus_intr_disable(pci_chipset_tag_t pc __unused, int irq)
{
	int byte = (irq / 8), bit = (irq % 8);

#if 1
	printf("eb64plus_intr_disable: disabling %d (%d:%d)\n", irq, byte, bit);
#endif
	eb64plus_intr_mask[byte] |= (1 << bit);

	bus_space_write_1(eb64plus_intrgate_iot, eb64plus_intrgate_ioh, byte,
	    eb64plus_intr_mask[byte]);
}
#endif
