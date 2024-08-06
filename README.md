# xrandr-setup
xrandr-setup is a simple XRandR wrapper aimed to make multimonitor
setups especialy with hybrid graphics laptops, work.

While using laptops with hybrid graphics, monitor identifiers tend to
change, making it difficult to have a single config that just works
in all cases.

This application combats it using a configuration file in which layouts
can be defined for multiple combinations of monitor ids. The application
gets your current screen configuration and applies the first matching
settings defined.

Moreover a prompt can be used, for example [dmenu](https://tools.suckless.org/dmenu/),
to switch between different configs matching the current monitor layouts.

## Requirements

- libx11
- libxrandr
- a menu selector application such as [dmenu](https://tools.suckless.org/dmenu/)

## Installation
Set your desired install location in the Makefile and to install just execute:
```bash
sudo make clean install
```

## Running xrandr-setup

xrandr-setup can take the following input arguments:

### `--auto` (or `-a`)
xrandr-setup sets all connected displays at their maximum mode (resolution and refresh rate)
at an offset of 0, basicaly duplicating them.

### `--select` (or `-s`)
xrandr-setup parses the configuration and prompts through a menu all the valid layouts that
can be selected with the connected displays. If none is valid, it behaves like `--auto`.

All arguments after `-s` or `--select` are passed to the menu application.

### No input arguments
xrandr-setup parses the configuration and selects the first layout that matches the connected
displays. If none is valid, it behaves like `--auto`.

## Configuration

The default configuration path is (can be changed through a constant variable in the `main.c` file):
```bash
$XDG_CONFIG_HOME/xrandr-setup/xrandr-setup.toml
```

Configuration is in toml like format, so the following rules apply:
- Whitespace is ignored
- All options and array definitions must be on a new line.
- Options are defined only once per array.
- Format of options is `option=value`
- Comments are prepended with the `#` character.
- Strings must be enclosed in `"` characters.

### Screen

The screen options include:
| Name     | Type   | Description                                                          |
|:---------|:-------|:---------------------------------------------------------------------|
| name     | string | The name of the layout, used with `--select` options.                |
| dpi      | uint   | The desired dpi of the screen.                                       |
| monitor  | list   | The monitor list. Must have one for each monitor in the layout.      |

### Monitor

Any option left empty except the `id` is set at its maximum allowed by XRandR.

The monitor options include:
| Name     | Type   | Description                                                          |
|:---------|:-------|:---------------------------------------------------------------------|
| id       | string | The id of the monitor as seen by XRandR.                             |
| xoffset  | uint   | The horizontal offset of the monitor in the screen.                  |
| yoffset  | uint   | The vertical offset of the monitor in the screen.                    |
| xmode    | uint   | The horizontal resolution of the monitor.                            |
| ymode    | uint   | The verical resolution of the monitor.                               |
| rate     | double | The refresh rate of the monitor.                                     |
| rotation | string | The rotation of the monitor (`normal`, `inverted`, `left`, `right`). |
| primary  | bool   | Sets the monitor as primary.                                         |

### Example
```
[[screen]]
        name="External monitor"
        #dpi=96
        [[monitor]]
                id="HDMI-0"
                primary=true
        [[monitor]]
                id="eDP-1"
                xoffset=1920

[[screen]]
    name="Internal monitor"
    dpi=96
    [[monitor]]
        id="eDP-1"
        primary=true
```

## Logging
As xrandr-setup is expected that most times it will not be executed directly, but through
a window manager, every warning and error is logged at a file, by default set to:
`$HOME/window-manager.log`
