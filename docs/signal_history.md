# neumoDVB #

## Signal history ##

When tuning to muxes, neumoDVB periodically saves the SNR, signal strength and BER for the tuned mux.
This information can be inspected using the `signal history` command in the `Control` menu.
This allows you to discover degradation of the signal over time, e.g., as trees grow, or as a satellite
deviates from its correct orbit, or as the signal degrades using a rainstorm.

![Positioner control panel](images/signal_history.png)


Note that neumoDVB distinghuishes between signals received from different LNBs and using different tuners.
In the screen shot a mux on Astra 2 was tuned with two different cards: TBS6909X (C0) and TBS6903X (C2).
The TBS6909X also used two different LNBs installed on two dishes: dish 0 and dish 3. The "#0" and"#3"
refer to the RF connectors on the cards to which the LNBs are connected.
