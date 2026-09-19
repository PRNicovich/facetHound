# Settings/menu protocol

The display renders the menu. The base owns machine settings, persistent mark
points, and the physical keyboard. Messages are newline-delimited ASCII on the
existing 460800-baud display UART. USB Serial accepts the same commands for
proxy testing.

## Physical controls

The base suppresses normal motor actions while the menu is open.

- Top-left/former wheel-index key: open menu; back in menus; coarser tier while
  editing a number.
- Main twist wheel left/right: menu up/down.
- Main twist wheel click: select or save.
- Former servo key: finer numeric-edit tier.
- Former cheat key: delete the selected mark point.

The constants are grouped near the top of `baseChassisModule.ino` if a keypad's
HID assignments need to be changed.

## Menu control

```text
base -> display: @MENU,TOGGLE
display -> base: @MENU,OPEN
base -> display: @MENUKEY,UP
base -> display: @MENUKEY,DOWN
base -> display: @MENUKEY,SELECT
base -> display: @MENUKEY,BACK
base -> display: @MENUKEY,FINER
base -> display: @MENUKEY,DELETE
display -> base: @MENU,CLOSED
```

The display also accepts `@MENU,?` and replies with its current state. It sends
that state with its one-second heartbeat, allowing the base safety interlock to
self-correct after a dropped packet. `@MENU,1`/`OPEN` and `@MENU,0`/`CLOSE`
remain supported for diagnostics.

`OPEN`/`CLOSE`, `OK`/`ESC`, and an explicit `COARSER` menu key are also
accepted.

## One snapshot on entry

```text
display -> base: @CFGGET,ALL
base -> display: @CFGSYNC,BEGIN
base -> display: @CFG,INDEX_DIRECTION,CW
base -> display: @CFG,TABLE_ADAPTER,OFF
base -> display: @CFG,SERVO_ENABLED,OFF
base -> display: @CFG,WHEEL_INDEX,96.0000
base -> display: @CFG,Z_POLARITY,NORMAL
base -> display: @CFG,FLOW_CONVERSION,0.052000
base -> display: @CFG,INDEX_SPIN_RPM,0.000000
base -> display: @CFG,POSITION_COUNT,16
base -> display: @CFGSYNC,END
```

Machine settings do not stream continuously. Mark-point values are the one
exception: they are fetched in small pages only while the list is visible, so
an arbitrarily long runtime list does not consume a second full copy of RAM on
the display.

```text
display -> base: @CFGGET,POSITIONS,0,8
base -> display: @CFGSYNC,POSITIONS_BEGIN,0,8
base -> display: @CFG,POSITION_0,0.0000
...
base -> display: @CFGSYNC,POSITIONS_END
```

## Writes and actions

```text
@CFGSET,DISPLAY_MODE,CLASSIC
@CFGSET,INDEX_DIRECTION,CW
@CFGSET,TABLE_ADAPTER,ON
@CFGSET,SERVO_ENABLED,ON
@CFGSET,WHEEL_INDEX,96
@CFGSET,Z_POLARITY,REVERSED
@CFGSET,FLOW_CONVERSION,0.052000
@CFGSET,POSITION_12,72.5000

@CFGACTION,RESET_POSITIONS,4
@CFGACTION,ADD_POSITION,0
@CFGACTION,DELETE_POSITION,12
@CFGACTION,LOAD_SD_FILE,0
@CFGACTION,ZERO_ENCODER,INDEX
@CFGACTION,ZERO_ENCODER,TIP
@CFGACTION,ZERO_ENCODER,Z
@CFGACTION,ZERO_ENCODER,ALL
@CFGACTION,INDEX_SPIN_START,-1.25
@CFGACTION,INDEX_SPIN_STOP,0
```

Reset spacing accepts 4, 8, 12, 16, or 32. SD files are fetched in six-name
pages with `@CFGGET,SD_FILES,start,6`; the base reports `SD_STATUS`,
`SD_FILE_COUNT`, and `SD_FILE_n` rows. A successful selection returns
`@CFGACK,LOAD_SD_FILE,title`, followed by periodic
`@JOB,tier,facet,angle,gemcad-distance,index,name` rows. The distance field is
retained as file metadata and is never treated or displayed as machine Z.
Applied writes use
`@CFGACK,id,value`; rejected writes use `@CFGNAK,id,reason`.

When Table Adapter is ON, the base reports:

```text
tipDegrees = calibratedTipDegrees - 45.0
```

Changing index direction safely stops twist correction, changes the encoder
and motor sign convention, and reacquires the current twist position before
allowing a new correction.

Continuous index spin accepts signed 0.01-10 RPM. While running, the display
renews the command every 500 ms; the base hard-stops after 1.5 seconds without
a renewal. Leaving the spin page also sends an explicit stop.

## Runtime gem mesh

After building a loaded ASC/FCT design, the base streams `MESHBEGIN`, sequential
`MESHV` vertices, `MESHE` edges, `MESHP` facet poses/names, and `MESHEND`. Edge
rows contain a compact list of supporting plane ids for facet highlighting.
Each plane row retains the imported name (`C1`, `P1`, `G2`, `T`, etc.) for the
tip-angle HUD. The base waits for `@MESHACK,BEGIN`, then sends one geometry row
per 5 ms without blocking its main loop. The display suspends rendering during
receipt. Transfer is activated only when every declared row arrives in order.
`@MESHACK,READY` closes settings, resets the view pose and redraws the current
mode with the new mesh. A failed transfer shows a reload error instead of
rendering the built-in mesh with the loaded job's points. The base reports
`@GEM,DISPLAY_READY` or `@GEM,DISPLAY_ERROR,...` to USB; `@GEM,LOADED,...`
alone means the base has loaded/built geometry, not that the display has it.
The display requests recovery with `@CFGGET,MESH` at boot. Both base and
display firmware must be updated together for the BEGIN handshake.

Runtime limits are 512 planes, 1,024 vertices, and 2,048 edges.

## Mark-point capacity

The base uses `std::vector<float>` at runtime, so there is no fixed 16-point
logic limit. Capacity is bounded only by available RAM. The included raw-flash
record reserves the final 64 KiB and persists up to 16,320 float positions;
larger runtime lists are valid but cannot be saved by this record format.
