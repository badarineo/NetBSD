/* $NetBSD: tegra_com.c,v 1.15 2021/01/27 03:10:19 thorpej Exp $ */

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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

#include <sys/cdefs.h>

__KERNEL_RCSID(1, "$NetBSD: tegra_com.c,v 1.15 2021/01/27 03:10:19 thorpej Exp $");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/termios.h>

#include <arm/nvidia/tegra_reg.h>
#include <arm/nvidia/tegra_var.h>

#include <dev/ic/comvar.h>

#include <dev/fdt/fdtvar.h>

static int tegra_com_match(device_t, cfdata_t, void *);
static void tegra_com_attach(device_t, device_t, void *);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "nvidia,tegra210-uart" },
	{ .compat = "nvidia,tegra124-uart" },
	{ .compat = "nvidia,tegra20-uart" },
	DEVICE_COMPAT_EOL
};

struct tegra_com_softc {
	struct com_softc tsc_sc;
	void *tsc_ih;

	struct clk *tsc_clk;
	struct fdtbus_reset *tsc_rst;
};

CFATTACH_DECL_NEW(tegra_com, sizeof(struct tegra_com_softc),
	tegra_com_match, tegra_com_attach, NULL, NULL);

static int
tegra_com_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
tegra_com_attach(device_t parent, device_t self, void *aux)
{
	struct tegra_com_softc * const tsc = device_private(self);
	struct com_softc * const sc = &tsc->tsc_sc;
	struct fdt_attach_args * const faa = aux;
	bus_space_tag_t bst = faa->faa_bst;
	bus_space_handle_t bsh;
	char intrstr[128];
	bus_addr_t addr;
	bus_size_t size;
	u_int reg_shift;
	int error;

	if (fdtbus_get_reg(faa->faa_phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}

	if (of_getprop_uint32(faa->faa_phandle, "reg-shift", &reg_shift)) {
		/* missing or bad reg-shift property, assume 2 */
		reg_shift = 2;
	}

	sc->sc_dev = self;

	tsc->tsc_clk = fdtbus_clock_get_index(faa->faa_phandle, 0);
	tsc->tsc_rst = fdtbus_reset_get(faa->faa_phandle, "serial");

	if (tsc->tsc_clk == NULL) {
		aprint_error(": couldn't get frequency\n");
		return;
	}

	sc->sc_frequency = clk_get_rate(tsc->tsc_clk);
	sc->sc_type = COM_TYPE_TEGRA;

	error = bus_space_map(bst, addr, size, 0, &bsh);
	if (error) {
		aprint_error(": couldn't map %#" PRIxBUSADDR ": %d", addr, error);
		return;
	}

	com_init_regs_stride(&sc->sc_regs, bst, bsh, addr, reg_shift);

	com_attach_subr(sc);
	aprint_naive("\n");

	if (!fdtbus_intr_str(faa->faa_phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error_dev(self, "failed to decode interrupt\n");
		return;
	}

	tsc->tsc_ih = fdtbus_intr_establish_xname(faa->faa_phandle, 0,
	    IPL_SERIAL, FDT_INTR_MPSAFE, comintr, sc, device_xname(self));
	if (tsc->tsc_ih == NULL) {
		aprint_error_dev(self, "failed to establish interrupt on %s\n",
		    intrstr);
	}
	aprint_normal_dev(self, "interrupting on %s\n", intrstr);
}

/*
 * Console support
 */

static int
tegra_com_console_match(int phandle)
{
	return of_compatible_match(phandle, compat_data);
}

static void
tegra_com_console_consinit(struct fdt_attach_args *faa, u_int uart_freq)
{
	const int phandle = faa->faa_phandle;
	bus_space_tag_t bst = faa->faa_bst;
	bus_space_handle_t dummy_bsh;
	struct com_regs regs;
	bus_addr_t addr;
	tcflag_t flags;
	u_int reg_shift;
	int speed;

	fdtbus_get_reg(phandle, 0, &addr, NULL);
	speed = fdtbus_get_stdout_speed();
	if (speed < 0)
		speed = 115200;	/* default */
	flags = fdtbus_get_stdout_flags();

	if (of_getprop_uint32(faa->faa_phandle, "reg-shift", &reg_shift)) {
		/* missing or bad reg-shift property, assume 2 */
		reg_shift = 2;
	}

	memset(&dummy_bsh, 0, sizeof(dummy_bsh));
	com_init_regs_stride(&regs, bst, dummy_bsh, addr, reg_shift);

	if (comcnattach1(&regs, speed, uart_freq, COM_TYPE_TEGRA, flags))
		panic("Cannot initialize tegra com console");
}

static const struct fdt_console tegra_com_console = {
	.match = tegra_com_console_match,
	.consinit = tegra_com_console_consinit,
};

FDT_CONSOLE(tegra_com, &tegra_com_console);
