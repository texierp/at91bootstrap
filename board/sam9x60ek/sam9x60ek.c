/* ----------------------------------------------------------------------------
 *         Microchip Microprocessor (MPU) Software Team
 * ----------------------------------------------------------------------------
 * Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Microchip's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY MICROCHIP "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL MICROCHIP BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "common.h"
#include "hardware.h"
#include "arch/at91_sfr.h"
#include "arch/at91_rstc.h"
#include "arch/at91_pmc/pmc.h"
#include "arch/at91_smc.h"
#include "arch/at91_pio.h"
#include "arch/at91_ddrsdrc.h"
#include "gpio.h"
#include "pmc.h"
#include "usart.h"
#include "debug.h"
#include "ddramc.h"
#include "timer.h"
#include "watchdog.h"
#include "string.h"
#include "sam9x60ek.h"
#include "board_hw_info.h"
#include "twi.h"

#define PLLA_DIV 1
#define PLLA_COUNT 0x3f
#define PLLA_LOOP_FILTER 0
#define PLLA_CLOCK 200000000
#define PLLA_FRACR(_p, _q) \
	((unsigned int)((((unsigned long)(_p)) << 22) / (_q)))

static void at91_dbgu_hw_init(void)
{
	/* Configure DBGU pins */
	const struct pio_desc dbgu_pins[] = {
		{"RXD", AT91C_PIN_PA(9), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"TXD", AT91C_PIN_PA(10), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{(char *)0, 0, 0, PIO_DEFAULT, PIO_PERIPH_A},
	};

	pio_configure(dbgu_pins);

	pmc_enable_periph_clock(AT91C_ID_DBGU);
}

static void initialize_dbgu(void)
{
	at91_dbgu_hw_init();
	usart_init(BAUDRATE(MASTER_CLOCK, BAUD_RATE));
}

#ifdef CONFIG_DDR2

static void ddramc_reg_config(struct ddramc_register *ddramc_config)
{
	ddramc_config->mdr = (AT91C_DDRC2_DBW_16_BITS |
				AT91C_DDRC2_MD_DDR2_SDRAM);

	ddramc_config->cr = (AT91C_DDRC2_NC_DDR10_SDR9 |
				AT91C_DDRC2_NR_14 |
				AT91C_DDRC2_CAS_3 |
				AT91C_DDRC2_WEAK_STRENGTH_RZQ7 |
				AT91C_DDRC2_NB_BANKS_8 |
				AT91C_DDRC2_DECOD_INTERLEAVED |
				AT91C_DDRC2_UNAL_SUPPORTED);

	ddramc_config->rtr = 0x30e;

	ddramc_config->t0pr = (AT91C_DDRC2_TRAS_(8)
				| AT91C_DDRC2_TRCD_(3)
				| AT91C_DDRC2_TWR_(3)
				| AT91C_DDRC2_TRC_(11)
				| AT91C_DDRC2_TRP_(3)
				| AT91C_DDRC2_TRRD_(2)
				| AT91C_DDRC2_TWTR_(2)
				| AT91C_DDRC2_TMRD_(2));

	ddramc_config->t1pr = (AT91C_DDRC2_TXP_(2)
				| AT91C_DDRC2_TXSRD_(200)
				| AT91C_DDRC2_TXSNR_(28)
				| AT91C_DDRC2_TRFC_(26));

	ddramc_config->t2pr = (AT91C_DDRC2_TFAW_(7)
				| AT91C_DDRC2_TRTP_(2)
				| AT91C_DDRC2_TRPA_(3)
				| AT91C_DDRC2_TXARDS_(2)
				| AT91C_DDRC2_TXARD_(8));
}

static void ddramc_init(void)
{
	struct ddramc_register ddramc_reg;
	unsigned int reg = 0;

	reg = readl(AT91C_BASE_SFR + SFR_DDRCFG);
	reg |= (AT91C_EBI_CS1A | AT91C_EBI_DDR_MP_EN);
	writel(reg, (AT91C_BASE_SFR + SFR_DDRCFG));

	pmc_enable_periph_clock(AT91C_ID_MPDDRC);
	pmc_enable_system_clock(AT91C_PMC_DDR);

	ddramc_reg_config(&ddramc_reg);

	/* Configure Shift Sampling Point of Data. */
	reg = AT91C_MPDDRC_RD_DATA_PATH_ONE_CYCLES;
	writel(reg, (AT91C_BASE_MPDDRC + MPDDRC_RD_DATA_PATH));

	/* MPDDRC I/O Calibration Register */
	reg = readl(AT91C_BASE_MPDDRC + MPDDRC_IO_CALIBR);
	reg &= ~AT91C_MPDDRC_RDIV;
	reg |= AT91C_MPDDRC_RDIV_DDR2_RZQ_100;
	writel(reg, (AT91C_BASE_MPDDRC + MPDDRC_IO_CALIBR));

	ddram_initialize(AT91C_BASE_MPDDRC, AT91C_BASE_CS1, &ddramc_reg);

	ddramc_dump_regs(AT91C_BASE_MPDDRC);
}
#endif /* CONFIG DDR2 */

static void at91_green_led_on(void)
{
	pio_set_gpio_output(AT91C_PIN_PB(13), 0);
	pio_set_gpio_output(AT91C_PIN_PB(11), 0);
	pio_set_gpio_output(AT91C_PIN_PB(12), 1);
}

#ifdef CONFIG_HW_INIT
void hw_init(void)
{
	struct pmc_pll_cfg plla_config;

	/* Disable watchdog */
	at91_disable_wdt();

	/* Green Led ON */
	at91_green_led_on();

	/* Configure & Enable PLLA */
	plla_config.mul = 49;
	plla_config.div = PLLA_DIV;
	plla_config.count = PLLA_COUNT;
	plla_config.fracr = 0;
	plla_config.loop_filter = PLLA_LOOP_FILTER;
	pmc_sam9x60_cfg_pll(PLL_ID_PLLA, &plla_config);

	pmc_cfg_mck(BOARD_PRESCALER_PLLA);

	/* Initialize dbgu */
	initialize_dbgu();

	/* Enable External Reset */
	writel(AT91C_RSTC_KEY_UNLOCK | AT91C_RSTC_URSTEN, AT91C_BASE_RSTC + RSTC_RMR);

	/* Init timer */
	timer_init();

#ifdef CONFIG_DDR2
	/* Initialize DDRAM Controller */
	ddramc_init();
#endif
}
#endif /* #ifdef CONFIG_HW_INIT */

#ifdef CONFIG_SDCARD
#ifdef CONFIG_OF_LIBFDT
void at91_board_set_dtb_name(char *of_name)
{
	strcpy(of_name, "at91-sam9x60ek.dtb");
}
#endif

#define ATMEL_SDHC_GCKDIV_VALUE     5

void at91_sdhc_hw_init(void)
{
#ifdef CONFIG_SDHC0
	const struct pio_desc sdmmc_pins[] = {
		{"SDMMC0_CMD",	AT91C_PIN_PA(16), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"SDMMC0_CK",	AT91C_PIN_PA(17), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"SDMMC0_DAT0",	AT91C_PIN_PA(15), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"SDMMC0_DAT1",	AT91C_PIN_PA(18), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"SDMMC0_DAT2",	AT91C_PIN_PA(19), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{"SDMMC0_DAT3",	AT91C_PIN_PA(20), 0, PIO_DEFAULT, PIO_PERIPH_A},
		{(char *)0, 0, 0, PIO_DEFAULT, PIO_PERIPH_A},
	};
	pio_configure(sdmmc_pins);

	pmc_enable_periph_clock(CONFIG_SYS_ID_SDHC);
	pmc_enable_generic_clock(CONFIG_SYS_ID_SDHC,
				 GCK_CSS_PLLA_CLK,
				 ATMEL_SDHC_GCKDIV_VALUE);
#endif
}
#endif /* #ifdef CONFIG_SDCARD */