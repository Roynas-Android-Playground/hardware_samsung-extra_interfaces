package vendor.samsung_ext.framework.battery;

@VintfStability
interface ISmartCharge {
    /**
     * Sets the charging policy thresholds.
     * This operation may be called while the policy is active; the running
     * worker re-evaluates the new policy immediately.
     *
     * @param upper stop charging at or above this percentage (50-95).
     * @param lower resume charging at or below this percentage, or -1 for
     *              stop-only mode.
     * @throws IllegalArgumentException for an invalid threshold pair.
     */
    void setChargeLimit(in int upper, in int lower);

    /**
     * Idempotently enables or disables SmartCharge.
     * Re-enabling with a different restart value updates the running mode.
     * Disabling always attempts to restore charging permission. Transient
     * Health/backend failures fail open and are retried while enabled.
     *
     * @param enable whether the policy should run.
     * @param restart whether hysteresis restart mode should be used.
     * @throws UnsupportedOperationException if no device backend is available.
     * @throws IllegalStateException if valid thresholds are not configured.
     * @throws IllegalArgumentException if restart mode has no lower threshold.
     */
    void activate(in boolean enable, in boolean restart);
}
