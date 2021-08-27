# neumoDVB #

## Positioner control ##

neumo DVB can operator DiSEqC positioners, also called "rotors". DiSEqC positioners are controlled
using one of two protocols:

* USALS or DiSeqC 1.3 is the easiest to use: the receiver tells the motor how many degrees to rotate.
  This rotation angle is *not* the orbital position but is computed from it. This requires knowledge of
  where your dish is located (latitude and longitude)
* DiSeQc 1.2 is more complicated: the user first manually positions the rotor (by pressing buttons on the
  receiver) until good reception is achieved, Then the user presses another button on the receiver to
  store this position under a preset number. Future tunes will then ask the positioner to go to the correct
  preset.

Before using a positioner, it is essential that you enter the latitude and longitude in
neumoDVBs config file, usually `~/.config/neumodvb/neumodvb.cfg`. These numbers will be shown
on the positioner panel, but changes made in `USALS Location` on the panel are not yet saved automatically
in this file. This may change in a future version.


The positioner control panel is shown in the screenshot below. The bottom part contains information and
controls which also appear on the spectrum screen. They operate in the same way and will not discuss them here.


![Positioner control panel](images/positioner.png)


The top panel, from left to right, has the following sub-panels:

* `Motor control`allows switching between USALS, DiSeQc1.2 positioners, slave LNBs (which are LNBs on a
  moving dish, but without the possibility to send commands to the positioner)  and a setup without a positioner.
  The documentation for the LNB screen provides related information.

  Despite its name, the Positioner Control panel is also useful for LNBs on fixed dishes. In this case,
  the top panels are not useful. but the bottom panels allow you to easily tune to muxes, verify signal levels
  ...

 * `Drive USALS motor` allows you to send the positioner manually to a specific satellite position,
   by entering it in text form in various formats: 5.0W, -5.0, ... The `East` and `West buttons` ask for
   a small step in the specified direction. The size of the step can be changed using the `step` spin control.
   which is currently set at 0.1 degree.  `HALT` stops the motor, `Set` activates the textual value entered (but you
   can also press the `ENTER` key). `Reset` changes the current USALS position to the one specified in the
   network

 * `USALS Location` allows you to enter your dish's location. Changes to the values will be taken into
   account the next time any USALS operation is performed. Unfortunately changes are not yet written
   to neumoDVB's config file, so you will need to edit that file to make permanent changes.

 * `Continuous motion and limits.` These buttons complement the ones on the USALS panel.
   While the `East` and `West buttons` on the USALS panels allow you to move the dish in small steps,
   `Go East` and `Go West` will drive the dish continuously. Motion will stop, until you press `Halt`
   (on the USALS panel), until your dish reaches internal limits, or until it hits a wall or some other
   obstacle. Use at your own risk. Often it is better to use the `East` and `West` buttons on the USALS
   panel. These buttons also work in DiSEqC 1.2 mode. `Go Ref` goes to the reference position. If your dish
   is properly installed, it will move due south.

   To avoid damage, it is possible to set software limits on the range in which the dish can move. To use this
   feature, first move the dish using any of the positioner commands to the west-most position you consider
   safe. Then press `Set West`. From this point on, the dish will refuse to move more west that this. The
   `Set East` button operates similarly. If the set limits are too narrow, your can remove them by pressing
   the `No limits` button.


 * `Diseqc 1.2` allows you to enter your dish's location. First,  move the dish using any of the positioner
   commands until you receive the satellite you want. The tuning facilities at the bottom can be useful
   for fine tuning. Once you are satisfied with the current position, enter a preset number in the text field
   on the DiSEqC1.2 panel and press `Store pos.` to associate the current dish position with the preset.
   Obviously, it is important to select a different preset number for different satellite positions.
   The `Goto pos.` button allows you to move the dish manually to a specific preset position.

   *Very important*. After making changes, these changes are not automatically saved. You need to press the
   `Save` button on the `LNB and sat` panel to save the preset position into the database. In case you forget,
   neumoDVB will remind you when you try to close the positioner control.
