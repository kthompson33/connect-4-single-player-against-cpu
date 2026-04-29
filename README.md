## Human vs. Computer Connect 4 Add-on

An automated physical add-on to a standard Connect 4 board with advanced CPU strategy, OLED interface, and IR move detection.

Introduction
The **Connect 4 Add-on** is a comprehensive modernization of the classic Connect 4 board game. Using a Raspberry Pi Pico, the system bridges physical play with digital intelligence. It is composed of a 3D printed device that sits on top of a standrd Connect 4 game, housing all of the components described below.  In this way it can be placed on top or removed at any time to covert the game from a conventional 2 player to a human vs. cpu game.  It monitors the board via 7 IR sensors to detect human moves, processes an CPU strategy using the Minimax algorithm, and communicates with the player through a high-visibility OLED display and addressable NeoPixel LEDs. 

## Features
- **6-Level CPU Strategy:** Ranges from basic win/block logic to a 6-move deep "Oracle" CPU using Alpha-Beta pruning.
- **Physical Piece Detection:** Automatic move tracking eliminates manual turn signaling; drop a piece and the game progresses.
- **Advanced OLED UI:** Text display for turn indicators and a system menu for clear visibility through the enclosure's viewport.
- **Multi-Function Control:** Integrated button logic for game level selection and menu navigation.
- **Move Rewind:** A dedicated Undo function that rolls back board logic turn-by-turn to correct mistakes.

## Hardware
### Components List
- **Microcontroller:** Raspberry Pi Pico
- **Display:** SSD1306 128x64 OLED (I2C)
- **Visuals:** 7-LED NeoPixel Strip (WS2812B)
- **Input Sensors:** 7x Infrared Obstacle Sensors (for column monitoring)
- **Input:** 1x Momentary Tactile Button
- **Power:** On/Off toggle switch and battery box to hold 3 AAA's

### Pinout Configuration
| Function | Pin |
| :--- | :--- |
| OLED SDA | GP4 |
| OLED SCL | GP5 |
| NeoPixel Data | GP22 |
| IR Sensors (1-7) | GP7, GP8, GP9, GP10, GP11, GP12, GP13 |
| IR Gatekeeper | GP20 |
| User Button | GP19 |

## How to Play
### Gameplay
1. **Initialize:** The game starts with a "NEW GAME!" message and a flashing LED light display.
2. **Your Turn:** When the OLED says "YOUR TURN", the LEDs will pulse Green above all columns which are not yet full.  Drop your checker through the top of the 3D printed housing that sits above the standard Connect 4 game.  The IR sensors embedded in the housing will record where you played.
3. **CPU Turn:** When the OLED says "MY TURN," one of the LEDs will pulse Blue over its chosen column. Drop a checker into that column for the CPU.
4. **Win/Tie:** The system automatically detects the end of the game, displays the winner, and runs a celebration animation. Tap the button to reset.

### Controls
- **Change Level:** Short press the button to cycle through Levels 1-6.
- **Menu Access:** Long press (2 seconds) to open the menu.
  - **Short Press:** Navigate between "Reset" (restarts the entire game) and "Undo" (undo the prior move).
  - **Long Press:** Execute the action.  
- **Undo Move:** Selecting "Undo" rewinds the board logic by one move and pauses. Press the button again when the physical piece has been removed to resume play.
