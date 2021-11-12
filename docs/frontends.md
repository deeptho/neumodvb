# neumoDVB #

## Configuring the frontends ##

The first test you should now perform is to check whether neumoDVB can access your DVB devices.
From the `lists` menu, choose `Frontends` (Ctrl-Shift-F). For each frontend in `/dev/dvb/adapter/...`
one entry should be listed.

![Frontends](images/frontends.png)

There is not much to configure here, but the following cmay be useful. Note that you need
first enable `Edit mode' (Alt-E).

*`enabled`. Disable a frontend by unchecking this option.

*`priority`. Frontends with a higher value will be preferred for tuning to services.
check buttons is not very user friendly, but you will get the hang of it. Priority values should be
positive numbers. The value -1 means "don't care."

*`master`. Enter the number of another frontend here to indicate that the current frontend is a "slave"
tuner, which reuses the RF frontend of the other one. Slave tuners can only tune to muxes in the same
RF band (low or high Ku band) and with the same polarization as master tuners. neumoDVB will only use
them if the master tuner is already active and tuned to a compatible RF-band and polarization.

The example shows a TBS 6909x card, which has 8 adapters. The odd numbered ones are slaves to the
even-numbered ones,
