# Spotify Controler
 a versatile spotify controler that uses the ESP8266 wifi features to comunicate with the spotify API, it also includes the capability to use a 20x4 LCD screen over I2C

## features
- pause/play
- skip/go back
- like song
- display song name, progress and duration
- IT SHOWS YOU THE TIME (sometimes...)

## how to make the code work (legacy)
The procces to make this work for your own projects is quite long but worth it at the end, just follow this steps and you will have the code ready in no time:
- replace the WIFI_SSID and PASSWORD with your local house wifi name and password
- go to [spotify for developers](https://developer.spotify.com) and create an account
- with that account go to the dashboard and click on "create app"
- put literaly anything on name and description, put "http://localhost:8888" on redirect URI and click save
- go to the app setings for the app you just created and look for the "client ID" and the "client secret"
- replace the CLIENT_ID and the CLIENT_SECRET if the code with your apps "client ID" and "client secret"
- upload your code and wait until the message "spotify setup" shows up on the LCD
- now go back to the settings of your spotify app and replace the recirect URI you put before with the URL that appears on the LCD with the word "callback" at the end, it should look something like this: "http://131.154.0.11/callback"
- put that same ULR in the code where it says REDIRECT_URI
- uplad the coad again
- ENJOY YOUR CONTROLER :)

## the electronics
### the components
- nodeMCU V1.0
- LCD 2004A with I2C adapter
- 4 mini pushdown switches
- 1 10K variable resistor
- some rando capacitors and resistors
- 0.5mm solid core wire
- A LITERAL FUCK TON OF PATIENCE TO SOLDER IT ALL TIGTH

### the wiring
i will probably make a diagram at some point, but im planing to do a youtube video first
It realy not that hard, connect the nodeMCU to the LCD via I2C, then the buttons to the nodeMCU on pins D5, D6, D7 and D8 with pull down resistors, connect the variable resistor to the ADC, 3.3v and with a pull-down to ground.

## the enclosure
the enclosure is 100% 3d printed with exception of some magnets so that it can stick to magnetic surfaces and some nylon screws and extensions that ussualy come with the LCD display
you can get the design on [OnShape](https://cad.onshape.com/documents/03c0f5b84e5dc7ce18a5b986/w/59c2b69761292aa5b35bee0f/e/642c8b87b2996676210efcd2)
The electronics come together inside the enclosure in a very specific way, if you solder everything outside the enclosure and then go to put it on you will have quite a rough time. most of the components are held together with a bunch of hot glue, hopes and prayers.

## BROKEN THINGS:
just look at the issues tab

<br>

[![forthebadge](https://forthebadge.com/images/badges/works-on-my-machine.svg)](https://forthebadge.com)

[![forthebadge](https://forthebadge.com/images/badges/powered-by-coffee.svg)](https://forthebadge.com)

[![forthebadge](https://forthebadge.com/images/badges/open-source.svg)](https://forthebadge.com)
