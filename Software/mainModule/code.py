import busio
import board
import displayio
import time
import binascii
from adafruit_hx8357 import HX8357
import fourwire
import terminalio
from adafruit_display_text import label

import keyboardHandling as kH


uartUSB = busio.UART(board.TX, board.RX, baudrate=400000)

uartMast = busio.UART(board.D12, board.D13, baudrate=115200)
uartBase = busio.UART(board.A4, board.A5, baudrate=115200)

DISP_BAUD = 500000000

dispHeight = 320
dispWidth = 480

disp_CS = board.D2
disp_DC = board.D3
disp_reset = board.D4


splashTime = 1 # seconds

TEXT_SCALE = 1


divLineColor = 0x888888

def initDisplay(disp_DC, disp_CS, disp_reset):

    # Init connection to display
    displayio.release_displays()

    spi = board.SPI()
    while not spi.try_lock():
        pass

    spi.configure(baudrate = DISP_BAUD)
    spi.unlock()


    displayBus = fourwire.FourWire(spi,
                                    command=disp_DC,
                                    chip_select = disp_CS,
                                    reset = disp_reset)


    print("display Init");
    display = HX8357(displayBus, width=dispHeight, height=dispWidth, rotation = 90)

    return display


def initMainScreen(display):

    main = displayio.Group()


    # Set text, font, and color
    text = "HELLO WORLD"
    font = terminalio.FONT
    color = 0xCCCC00

    # Create the text label
    text_area1 = label.Label(font, text=text, color=color, scale = 3, x = 100, y = 80)

    # Create the text label
    text_area2 = label.Label(font, text=text, color=color, scale = 3, x = 100, y = 200)

    text_area3 = label.Label(font, text=text, color=color, scale = 3, x = 100, y = 300)

    # Show it
    main.append(text_area1)
    main.append(text_area2)
    main.append(text_area3)

    display.root_group = main

    return text_area1, text_area2, text_area3


x = 0

uartUSB.reset_input_buffer()

display = initDisplay(disp_DC, disp_CS, disp_reset)
text_area1, text_area2, text_area3 = initMainScreen(display)

while True:

    if (uartUSB.in_waiting > 5):
        key = kH.readKeyboard(uartUSB, nBytes = 22, returnRaw = True)

        #print(key[1], len(key[1]))

        if key:
            outkey = kH.keyRouter(key[1][14])
            if outkey:
                #print(outkey)

                text_area1.text = outkey


    if (uartBase.in_waiting):

        rd = uartBase.readline().strip().decode()

        print(rd)

        readSplit = rd.split(" ")
        if (readSplit[0] == "F"):
            text_area2.text = readSplit[1]

        if (readSplit[0] == "Z"):
            text_area3.text = readSplit[1]


    #pitchText.text = "{:.2f}".format(x)
    #rollText.text = "{:.2f}".format(x)
    #mastText.text = "{:.2f}".format(x)



