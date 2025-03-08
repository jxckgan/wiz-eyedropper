# wiz-eyedropper

wiz-eyedropper is a simple C++ Qt utility designed to capture colour variables from a specific portion of the screen and then additionally controls (WiZ) LEDs in real-time, changing the colour of the said LEDs from the aforementioned captured information.

WiZ LEDs can be controlled from UDP requests if you're on the same local network ([see here](https://seanmcnally.net/wiz-config.html)), an example command (on macOS/Linux) for changing RGB values of the LED to red would be:

```sh
echo -n "{\"id\":1,\"method\":\"setPilot\",\"params\":{\"r\":255,\"g\":0,\"b\":0,\"dimming\": 75}}" | nc -u -w 1 192.168.1.XX 38899
```

### Building & Running

This app supports both macOS and Windows, to build this app ensure you have CMake and Qt 5.15.2 (for Windows `msvc2019_64`) installed. The following commands assume that you've cloned the repository and you're currently in the root of the repo in your terminal.

> Sidenote for Windows users, although not necassary (since you can use the official Qt Installer), I recommend using [aqt](https://github.com/miurahr/aqtinstall) for downloading prebuilt Qt binaries without needing an account.

On macOS, simply build this app like you would any CMake project and you'll have the app in `build/WizLedController` *(I've not implemented App Bundle support just yet)*

```sh
mkdir build
cd build
cmake ..
cmake --build .
```

On Windows, you'll have to specify the path to Qt 5.15.2. The following command builds a Release of the app, if you want to build for Debug then swap `Release` with `Debug`. The executable will be located in `build/Release/`

```sh
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Using the App

Simply put your WiZ IP address in the IP address bar, pick a colour on your screen, and it'll send that colour to your LEDs. It'll keep observing that section of the screen and update if the colour changes accordingly.

### Todo

- [ ] Allow multiple IP addresses for multiple LEDs
- [ ] Dark Mode for Windows
- [ ] Make the UI a bit nicer
- [ ] Potentially implement more LED brands

### Credits

Credit to Sean McNally for the UDP Code logic, found here: https://seanmcnally.net/wiz-config.html