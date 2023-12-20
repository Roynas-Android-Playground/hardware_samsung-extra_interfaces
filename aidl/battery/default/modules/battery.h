#pragma once

#include <stdbool.h>
#include <sys/cdefs.h>

/* Needs specific setChargable method */
#define SPECIFIC_SETCHARGE 0x1
/* Needs specific getPercent method */
#define SPECIFIC_GETPERCENT 0x2

__BEGIN_DECLS

/**
 * setChargable - Target specific method of enabling charge.
 *
 * Corresponding bitmask - SPECIFIC_SETCHARGE,
 * this must be set in available bitmask variable below, else will be ignored
 *
 * @param enable Enable charging or not
 * @return void
 */
extern void setChargable(const bool enable);

/**
 * getPercent - Target specific method of getting battery percentage
 *
 * Corresponding bitmask - SPECIFIC_GETPERCENT,
 * this must be set in available bitmask variable below, else will be ignored
 *
 * @param enable Enable charging or not
 * @return void
 */
extern int getPercent(void);

/* Bitmask OR-ed with methods needed to override
 * if a bit is not present it is ignored */
extern int availableMask;

__END_DECLS
