#ifndef BLACKOS_ACPI_H
#define BLACKOS_ACPI_H

/* Black OS - minimal ACPI support (power-off / report).
 *
 * acpi_init() scans the BIOS tables once at boot; acpi_poweroff() writes
 * SLP_EN|SLP_TYP to PM1a/b_CNT after parsing the DSDT _S5_ package.
 * No allocation, no interrupt dependence — safe from shell context.
 * Reference: ACPI 6.5, Linux drivers/acpi/ scan algorithms.  Credited in
 * THIRD_PARTY_NOTICES.md.
 */

void acpi_init(void);        /* call after PMM bring-up                       */
void acpi_poweroff(void);   /* SLP_EN|SLP_TYP(SLP_S5?) into PM1a/b_CNT      */
void acpi_reboot(void);     /* 8042 reset pulse                             */
void acpi_report(void);     /* diagnostic dump                               */

extern u8 g_acpi_slp_typ;   /* parsed _S5_ SLP_TYPa / SLP_TYPb value         */
extern u8 g_acpi_pmf;       /* preferred PM profile (informational)          */
extern u8 g_acpi_sci_int;   /* PM1x block SCI IRQ (informational)            */

#endif
