# Spoty Controler Documentation

## General Introduction
The Spoty Controler is a multi use interface with a 4x20 LCD display, 4 input buttons and a knob to interact with web APIs when it is connected to WIFI, it can be powered thru USB or with any 5v suply capable of delivering 800mA continiously.

## Features
- ESP8266(in nodeMCU v2)
- 802.11 b/g/n
- H02004DP-W 20x4 LCD display
- 4 buttons
- 1 analog knob
- micro USB B interface
- arduino firmwere allows customization

## Physical propieties

![body Drawing](https://drive.google.com/uc?id=10iffxCtLh2mflPl8C9erfC0Ad-KuQ2rG)

## Electrical diagram
missing(TODO)

## Fimwere

### introduction

The firewere is written mostly in C++ arduino with a small section of HTML for web control, 
Its fully customizable with most of the important functions defines in the main file for ease of editing,
It is in charge of all the user interface control, API connections and NTP up keep.

### startup

First it connect to the LCD to show the startup process screen, then it starts the actual process that follows this steps:
- try to connect to WIFI using the pre-saved SSIDs and SSPASes
- if it cant connect to any known networks it will show a new network screen that will show abvialable networks and ask for passwords for said networks
- connect to NTP and get network time
- generate the redirect URI 
- startup the HTTP server
- proceed to the loop and ask for configured auths
- startup finishes and main loop starts running the configured proceses

### interrupts

The firmwere relays on interrupts for button interactions, it has all the buttons atached to interrupts that direct to the same interrupt manager, this manager is already extreamly optimized and it's recomended to be left alone without modifications, this manager will check which pins are pulled high and change the global variable "pin called" to whichever pin was pressed.

### LCD manager

The LCD manager is the graphics manager of the device, 
it can generate the needed pre-programed screens and show them to the screens, 
it mainly works as a container for all the functions containing the actual information about the screen structures.

#### screens 

##### initial setup 

The setup screen is mainly decorative, it shows which process it taking place behind the covers, the version and debug messages in case anything goes wrong.

##### Spotify setup

This screen is shown while waiting for spotify auth and it shows the URI where the auth page has been set up

##### Waiting for device

this screen is only shown when spotify is connected but no device is connected and playing

##### Music screen

this is the main spotify API screen it will show all the info about the currently playing song, 
including duration, progress, name and more.
It also shows the time of NTP, and it can be configured to display 
extra information from other APIs on the top left corner.
This is the only screen where you can interact with the spotify API 
to for example skip, resume, pause, rewind, like or change the volume of the song.