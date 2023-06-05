package vendor.samsung_ext.framework.battery;

@VintfStability
interface ISmartCharge {
	/**
	 * Set a charge limit - the main function of this framework HAL.
	 * Negative value passed to the parameter [lower] are considered no-op.
	 * You must call #activate with [enable] to false first, or ensure impl
	 * is not running. else this will throw an IllegalStateException.
	 *
	 * @param upper Upper charge limit by percent of 100.
	 * @param lower Lower charge limit by percent of 100.
	 * @throws IllegalStateException if impl was running.
	 * @throws IllegalArgumentException if [upper] is
	 * less than 1, if [upper] is not higher than [lower]
	 * or if any of 2 does not fall under 1 ~ 100 range.
	 */
	oneway void setChargeLimit(in int upper, in int lower);

	/**
	 * Enable/Disable the charge limit framework.
	 * if [enable] is false, [restart] is ignored.
	 * You must call #setChargeLimit first or ensure that
	 * config props has valid value else this will
	 * throw an IllegalStateException.
	 *
	 * @param enable Enable the implementation
	 * @param restart Use the charge-restart method, if
	 * false, charging is stopped at [upper]
	 * @throws IllegalStateException if #setChargeLimit
	 * wasn't called before and config from property is invalid.
	 * @throws IllegalArgumentException if [enable] is
	 * true, and it is already enabled, and vice versa.
	 */
	oneway void activate(in boolean enable, in boolean restart);
}
