#pragma once

#include <stdbool.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

/**
 * setChargable - Target specific method of enabling charge.
 *
 * @param enable Enable charging or not
 * @return void
 */
extern void setChargable(const bool enable);

__END_DECLS
