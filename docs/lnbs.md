c# neumoDVB #
## Configuring LNBs ##

The LNB configurations informs neumoDVB about your satellite equipment setup. neumoDVB allows for complicated
setups involving multiple LNBs on multiple dishes, various combinations of DiSEqC switches, rotors etc.
It also aims to handle some tricky cases, such as multiple satellite positions being received by the same
LNB (e.g., closely spaced satellites like 9.0E and 10.0E), LNBs in `offset-positions' on movable dishes ...

All of this can get quite complicated (with many potential bugs) and is mostly handled by setting up an *LNB*
configuration, which is a bit of a misnomer as such a configuration describes not only the LNB but also
how it is connected to your adapters.

Apart from some general settings describing the LNB type, the most important properties of an LNB entry are `networks`
and `connections`.

A single LNB usually tunes to a single satellite, but there are exceptions: on smaller dishes close satellite positions
such as 9.0E and 10.0E can be received on the same LNB. Likewise, if the LNB is installed on a rotating dish, it can
tune to multiple (many) satellites. The `networks` property provides this information.

As an important difference with other programs, and with older versions of neumoDVB, each `LNB` line corresponds to exactly
one physical device. So a quad LNB for example will and should only appear once in the list and not four times like
in other programs. The `connections` property expresses how the LNB is connected to one or more RF inputs on one or more cards.

It is possible to install multiple LNBs on a rotating dish, but neumoDVB needs to be informed when LNBs will rotate
together on the same dish. The `dish` property provides that information.



### LNB setup ###

To add an LNB entry, visit the LNB screen by selecting `LNBs` in the list menu. This screen will still be empty.
Selecting `New` from the `Edit` menu will add one line to the table, which you will then need to edit.

![screenshot](images/lnb_list.png)

You need to edit only the following fields  in simple setups: `USALS pos`, `networks` and `connections`.

* `dish`. **Editable** If you have multiple dishes give them each a different number. This allows, e.g., using
  multiple LNBs aimed at the same satellite on multiple dishes. Setting this number correctly is also essential
  in case of a movable this: neumoDVB assumes that all LNBs on the same dish will move if the dish is
  moved. If the dish numbers differ, neumoDVB will assume that the LNBs are on different dishes.

* `on rotor`. **Editable** If you have a dish on a positioner, set this to true to inform neumoDVB that the dish
  is on a positioner. This field will also be set to True if any of the LNB's connections indicates that it can
  control a rotor. The opposite is however not true: it is possible to indicate that the LNB is on a rotor,
  but without allowing the user to control the dish.

* `Cur sat pos:` The actual satellite position the LNB points to. For an LNB on a positioner this will
  indicate the last known satellite to which the LNB points. This position may differ slightly from the true satellite
  position. For instance, for weaker satellites you may get a better signal by moving to a slightly "wrong" position if
  that reduces interference from a near by satellite.

  If your dish setup is imperfect, you may also need an USALS position different from the true satellite
  location.

  Using a "wrong" `USALS pos.` is also useful for other purposes. For instance, you could enter "10.0E"
  as the USALS. pos. for both the satellites 9.0E  and 10.0E. The dish will then remain at 10.0E if you tune
  to a service on 9.0E, reducing wear and tear if you switch
  frequently between both satellites.

  If you install an "offset LNB", by which we mean a second LNB not in the central position on a rotating dish,
  then this satellite position is the position pointed to by the LNB and it will differe from the "USALS position",.
  which is the satellite pointed to by the central LNB

  Note that this field is read only on this screen. It is estimated from `USALS pos` which can be set in the `networks` field
  or from the `positioner dialog`.

* `USALS pos:` This is the satellite position pointed to by the central LNB on a rotating dish. It also equals the USALS
  position used to control any USALS motor. For fixed LNBS, `USALS pos` is the location of the main network offered by the
  LNB. For Instance, for an LNB capable of receiving 9.0E and 10.0E, this will either be 9.0E or 10.0E.

* `offset_angle`. If you install a second (offset) LNB on a rotating dish, it will point to a different satellite
   than the central LNB and hence the USALS position needs to be adjusted when pointing the offset LNB. This number is estimated
   by neumoDVB and cannot be edited. It is the angle between the offset and central LNB w.r.t the rotor axis.

   In the above screen shot, the LNBs with `cur_sat_pos` 44.1E and 51.9E are both on the same dish, with 51.9 installed in
   the main central position, and the other LNB slightly (13.1 degrees) off-center.
   Currently the dish (its central LNB) is pointing to 51.9E. neumoDVB has computed that the C-band LNB now points to
   44.1E (nothing to receive there). If the C-band LNB is moved, e.g., to tune to 40.0E then these `cur_sat_pos` number
   will change for both LNBs. The C-band LNB will not be listed as 40.0E.

   Note that `offset_angle` is never set explicitly. Instead, after creating the first network, use the positioner dialog
   to set `USALS pos` for good reception. neumoDVB will then estimate `offset_angle`. This values is used to estimate
   `cur_sat_pos` and show it on screen. It is also used to compute an initial estimate for `USALS pos` for the 2nd, 3rd...
   networks you add. Therefore to configure an offset LNB, `USALS pos` has to be guessed by the user only once - for a single
   satellite. Afterwards, neumoDVB will provide a suitable default, which can then be fine-tuned if so desired.

* `ID`.  A unique ID to distinguish LNBs in case other parameters are not distinguishing enough.

* `enabled`. **Editable** To prevent neumoDVB from using an LNB, uncheck this box.
config/share/themes/Neumo/

* `available`.  This field will be unchecked if the LNB cannot be used now, e.g., because
  the card is  not in the system, or is not powered, or used in a virtual machine or not usable for any other reason.
  If the LNB is not available, the background is shown in red. This is the case for 19.2E in the screenshot, because the
  LNB is not connected to any card currently in the system.

* `LNB type`. **Editable** Change this in case of a C-band or Ka band LNB. Apart from providing a field to filter the list
  on (e.g., to show only Ku LNBs) this setting also provides reasonable defaults for various LNB parameters.

* `Networks`. **Editable** Double clicking on this field will popup a window, which lists all the
  networks that an LNB can tune to.

* `Connections`. **Editable** Double clicking on this field will popup a window, which lists all the
  connections (a connections is a specific RF input on a specific card) to the LNB.

  In the screenshot you will notice, that some connections are crossed out. This indicates that the required card
  is no longer in the system. For example, the first 23.5E LNB was connected to the TBS 5990, which is currently
  not in the system. It is also connected to `card 0`, which is a TBS 6909X that is still in the system
  and can therefore still be used.

  On the other hand, the connections in the red 19.2E LNB are mainly "garbage" connections left over from an older driver
  version which was incapable of identifying the card properly. So this is a mis-configuration that should be removed by the user.

  Note the importance of remembering old, currently not valid, connections: whenever the TBS 5990 is reinserted in the system,
  even in another USB slot, neumoDVB will detect its presence and allow the card to be used with the LNB, without any reconfiguration
  by the user. Also, these connections are stable against adapter renumbering.

* `pol type`. HV for a regular linear LNB, LR for a regular circular one. The other options indicate LNBs with
  swapped polarizations. E.g., a linear LNB rotated by 90 degrees or a circular one in which the depolarizing
  plate is rotated by 90 degrees.

* `prio`. **Editable** Change this to give preference to some LNBs when multiple ones can tune to the same
  satellite. LNBs with higher values are used preferentially. -1 means "default priority"

* `LOF offset`. After using an LNB for a while, this field will indicate any offset in the
  LNBs local oscillator (the two numbers are for the low and high band, respectively). The offset is determined by
  comparing the frequency reported by the driver with the one found in the NIT SI table. Unfortunately, some providers,
  including SKY-UK, sometimes provide incorrect frequency information in the NIT table. Also, some providers do not
  provide such information, and such information is completely missing on muxes which do not have broadcast transport streams.
  Therefore estimating the correct value is not trivial.

  The estimate is most reliable after tuning multiple muxes on the same LNB and incorrect values can make it difficult to tune
  narrow-band muxes. If you encounter problems tuning, then simply erase all found muxes and re-scan. In the second scan
  the estimated `LOF offset` will have stabilized. Note that such problems should be extremely rare.

* `freq min`. **Editable** This sets the lowest possible frequency the LNB can tune to. The special value
  -1 means that this is set to the default value for this LNB type, e.g., 10.7 GHz for a universal LNB.
  One reason to change the default is to  receive amateur radio transmissions on 26.0E: TBS5927 supports tuning
  lower frequencies. E.g., change this value to 10.5GHz.

* `freq mid`. **Editable** This sets the frequency which separates the low from the high band on the LNB
  The special value -1 means that this is set to the default value for this LNB type, e.g., 10.7 GHz for
  a universal LNB. For LNBs with only one band, set this value equal to `freq max`.

* `freq max`. **Editable**  This sets the highest possible frequency the LNB can tune to. The special value
  -1 means that this is set to the default value for this LNB type, e.g., 12.75 GHz for a universal LNB.

* `LOF low`. **Editable** This sets the local oscillator frequency for the low band on the LNB. The special
  value  -1 means that this is set to the default value for this LNB type.

* `LOF high`. **Editable**  This sets the local oscillator frequency for the high band on the LNB. The
  special value  -1 means that this is set to the default value for this LNB type.

### LNB networks setup ###

The screenshot below shows the networks for a movable dish which is allowed to move to many different
satellite positions. In this table, you can add new lines (`Edit - New`) or edit existing Lines (activate
`Edit mode` if needed):

 ![screenshot](images/lnb_networks.png)

The fields have the following meaning:

* `LNB Pos.` The official satellite position into this field.  This is the position as listed on satellite
  websites, and -- more importantly -- as listed in the service information in the satellite muxes.

  Some satellites broadcast unexpected values. e.g., 51.5E may report muxes on 52.0E or 53.0E.
  This can be confusing, e.g., if you expect services to be present
  in a list for 52.0E but they are instead on 53.0E. neumoDVB always considers the values in the SI stream
  to be the true ones, as long as the deviation is less than 1 degree.

* `USALS pos.`  The position sent to the USALS positioner. For an offset LNB this can differ (a lot) from
  the true satellite position. Usually it is best not to edit this field here: neumoDVB will then provide a default value,
  which will be correct as soon as one network for this LNB is correct.

  For the first network created, you will have to provide a value yourself, but it will probably be easier to do so
  from the positioner dialog.

* `priority` In case multiple LNBs point to the same satellite, neumoDVB will use the one which has the '
  correct network and has the highest priority LNB. If you have multiple dishes you can therefore indicate that
  the LNB on the biggest dish should be used to offer maximal rain fade margin. The other LNB on the smaller dish
  will then still be used by neumoDVB if the one on the larger dish cannot be used.

* `DiSEqC 1.2`: The DiSEqC command to send to move the dish, in case you do not use USALS. -1 means that no
  such command will be sent. Avoid DiSEqC 1.2 if possible. USALS is much simpler and much more convenient.

* `enabled`. **Editable** To prevent neumoDVB from using a connection, uncheck this box.

* `ref mux`. This is a readonly field. It is set by the positioner dialog and it is the default mux, which
  will be selected in that dialog.


### LNB connections setup ###

The screenshot below shows the LNB connections for an LNB on a fixes dish which is connected to multiple
cards, specifically to inputs 2 and 3 of the same TBS 6909X card (card 0) and to a TBS 5927 Card.
input 2 of the TBS 6909X is connected to a DiSEqC-11 switch, whose port 0 is connected to the
LNB. It is also connected to a DiSEqC-10 switch, whose port 0 is also connected to that LNB.
Finally the TBS 5927 is connected to another DiSEqC-10 switch, whose port 0 is connected to the
a third output to the same LNB. At some point in the past, the TBS 5990 was in the card
and was then connected to the cable currently attached to the TBS 5927.


In this table, you can add new lines (`Edit - New`) or edit existing Lines (activate
`Edit mode` if needed):

 ![screenshot](images/lnb_connections.png)

The fields have the following meaning:

* `Card RF#in` **Editable** The card and RF input connector to which the LNB is connected. Select
  it from the available choices in the popup list. Note that the same LNB can be connected to multiple
  cards or RF inputs (e.g., a Twin or Quad LNB). In this case, more than one entry should be created.
  Use the `New` button to add an extra connection.

* `enabled`. **Editable** To prevent neumoDVB from using an LNB, uncheck this box.

* `prio`. **Editable** Change this to give preference to some LNBs when multiple ones can tune to the same
  satellite. LNBs with higher values are used preferentially.

* `rotor`. **Editable** If you have a dish on a positioner, and the connection is wired to the rotor
  set this to `ROTOR_MASTER_USALS` (recommended) or `ROTOR_MASTER_DIEQC12`. This will cause the
  receiver to start sending DiSEqC commands to rotate the dish.
  In this case, you also need define the list of networks (satellites) that the positioner is allowed to
  move to in the LNB network list (see above).

  The value `ROTOR_SLAVE` means that the LNB is attached to a movable dish, but that the current
  connection is not connected to the rotor itself, and can therefore not be used to actually
  move the dish by sending DiSEqC commands. neumoDVB will use this LNB only if it determines that the
  dish is already pointing in the right direction.

  The default value `FIXED DISH` should be used for LNBs not on a movable dish

* `DiSEqC10`: the port number of the committed switch.  The first port on a switch is always named `0`, so for
DiSEqC10, the valid values are 0, 1, 2 and 3; -1 means "not present".

* `DiSEqC11`: the port number for the uncommitted switch. Valid values are  0...15. -1 means "not present".

If you use multiple "cascaded" switches, the order in which they are connected is also crucial for correct
operation:

* `tune_string`: determines what type of DiSEqC commands are sent, and which order they are sent in.
    `U`: uncommitted switch; `C`: committed switch; `P`: USALS command; `X`: DiSEqC12 command

    The default value 'UCP' means: first send the uncommitted command (DiSEqC11), then the committed
    command (DiSEqC10) and finally the command to rotate the dish (if applicable). Note that commands with port number
    -1 are skipped.

    Change the order to 'CUP' if your committed switch is connected directly to the receiver and the committed command should be
    sent first. If you wish to repeat some commands, just repeat the corresponding codes. For example, 'UCUCP'
    will send both switch commands twice. This is rarely needed, and neumoDVB will resend the commands anyway
    if tuning fails.

    If you need dieqc12 instead of USALS (not recommended: USALS is much easier and more versatile), then
    change this to `UCX`. Note that for DiSEqC12 the USALS position will still be changed in the GUI when the
    dish moves, but it will not be sent to the positioner. This USALS value is used by neumoDVB to
    remember the current position of the dish.
