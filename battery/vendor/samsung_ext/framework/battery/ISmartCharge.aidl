package vendor.samsung_ext.framework.battery;

@VintfStability
interface ISmartCharge {
	/**
	 * Set a charge limit - the main function of this framework HAL.
	 * Any negative value passed to the parameters are considered No-Op.
	 * You must call #activate with [enable] to false first, or ensure impl
	 * is not running. else this will throw an IllegalStateException.
	 *
	 * @param upper Upper charge limit by percent of 100.
	 * @param lower Lower charge limit by percent of 100.
	 * @throws IllegalStateException if impl was running.
	 * @throws IllegalArgumentException if [upper] is less than 1
	 * @throws IllegalArgumentException if [upper] is not higher than [lower]
	 */
	oneway void setChargeLimit(in int upper, in int lower);

	/**
	 * Enable/Disable the charge limit framework.
	 * if [enable] is false, [restart] is ignored.
	 * You must call #setChargeLimit first else this will
	 * throw an IllegalStateException.
	 *
	 * @param enable Enable the implementation
	 * @param restart Use the charge-restart method, if false,
	 *                charging is stopped at [upper]
	 * @throws IllegalStateException if #setChargeLimit wasn't called before.
	 */
	oneway void activate(in boolean enable, in boolean restart);
}
