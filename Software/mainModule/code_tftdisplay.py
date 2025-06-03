# Write your code here :-)
import board
import displayio
import terminalio
from adafruit_display_text import label
from adafruit_bitmap_font import bitmap_font
from adafruit_display_shapes.line import Line
import time
import binascii
from adafruit_hx8357 import HX8357

DISP_BAUD = 100000000

dispHeight = 320
dispWidth = 480

disp_CS = board.D2
disp_DC = board.D3
disp_reset = board.D4


splashTime = 3 # seconds

TEXT_SCALE = 1

mainFontFile = "fonts/spleen-32x64.bdf"
smallFontFile = "fonts/spleen-16x32.bdf"
labelFontFile = "fonts/t0-22-uni.bdf"
symbFontFile = "fonts/open_iconic_all_4x.bdf"
gemFontFile = "fonts/streamline_money_payments.bdf"

divLineColor = 0x888888

def initDisplay(disp_DC, disp_CS, disp_reset):

    # Init connection to display
    displayio.release_displays()

    spi = board.SPI()
    while not spi.try_lock():
        pass

    spi.configure(baudrate = DISP_BAUD)
    spi.unlock()


    displayBus = displayio.FourWire(spi,
                                    command=disp_DC,
                                    chip_select = disp_CS,
                                    reset = disp_reset)

    display = HX8357(displayBus, width=dispHeight, height=dispWidth, rotation = 90)

    return display


def drawSplashScreen(display, splashScreenTime = 0.1, useFont = '', gemFont = ''):

    # Draw initial display screen
    splash = displayio.Group()
    display.root_group = splash

    color_bitmap = displayio.Bitmap(dispHeight, dispWidth, 1)
    color_palette = displayio.Palette(1)
    color_palette[0] = 0x000000

    bg_sprite = displayio.TileGrid(color_bitmap,
                                   pixel_shader=color_palette,
                                   x=0, y=0)
    splash.append(bg_sprite)

    text_area = label.Label(
        gemFont,
        text=chr(57),
        color=0xFFFF00,
        scale=TEXT_SCALE*8,
        anchor_point=(0.5, 0.5),
        anchored_position=(160, 100),
    )
    splash.append(text_area)

    splashText = label.Label(
        dispFont,
        text=' FACET \n HOUND ',
        line_spacing = 1,
        color=0xFFFF00,
        scale=TEXT_SCALE,
        anchor_point=(0.5, 0.5),
        anchored_position=(160, 370),
    )
    splash.append(splashText)

    time.sleep(splashScreenTime)

    text_area.text=chr(72)

    return splash

def readMotorSpeedDirection(id):

    speed = 100
    dir = True
    return speed, dir

def drawMainScreen(display,
                    dispFont = None,
                    labelFont = None,
                    symbolFont = None,
                    smallFont = None):

    main = displayio.Group()
    display.root_group = main
    pitchText = label.Label(
                        dispFont,
                        text="--.--",
                        color=0xFFFF00,
                        scale=TEXT_SCALE,
                        anchor_point=(1, 1),
                        anchored_position=(315, 70),
                    )
    main.append(pitchText)

    pitchlabelText = label.Label(
                        labelFont,
                        text=chr(415),
                        color=0xFFFF00,
                        scale=TEXT_SCALE,
                        anchor_point=(0, 1),
                        anchored_position=(10, 70),
                    )
    main.append(pitchlabelText)

    pitchRollLine = Line(20, 90, 300, 90, divLineColor)
    main.append(pitchRollLine)


    rollText = label.Label(
                        dispFont,
                        text="---.--",
                        color=0x00FFFF,
                        scale=TEXT_SCALE,
                        anchor_point=(1, 1),
                        anchored_position=(315, 185),
                    )
    main.append(rollText)

    rolllabelText = label.Label(
                        labelFont,
                        text=chr(569),
                        color=0x00FFFF,
                        scale=TEXT_SCALE,
                        anchor_point=(0, 1),
                        anchored_position=(10, 185),
                    )
    main.append(rolllabelText)

    rollMastLine = Line(20, 205, 300, 205, divLineColor)
    main.append(rollMastLine)


    mastText = label.Label(
                        dispFont,
                        text="---.--",
                        color=0xFF00FF,
                        scale=TEXT_SCALE,
                        anchor_point=(1, 1),
                        anchored_position=(315, 310),
                    )
    main.append(mastText)


    zlabelText = label.Label(
                        labelFont,
                        text=chr(576),
                        color=0xFF00FF,
                        scale=TEXT_SCALE,
                        anchor_point=(0, 1),
                        anchored_position=(10, 310),
                    )
    main.append(zlabelText)

    underMastLine = Line(20, 330, 300, 330, divLineColor)
    main.append(underMastLine)


    mSpeed, mDir = readMotorSpeedDirection(0)
    if mDir:
        initChar = chr(66)
        flipChar = chr(67)
    else:
        initChar = chr(67)
        flipChar = chr(66)

    motorDirSign = label.Label(
                        symbolFont,
                        text = initChar,
                        color=0xFFFFFF,
                        scale=TEXT_SCALE,
                        anchor_point=(0.5, 0.5),
                        anchored_position=(30, 360)
                    )
    main.append(motorDirSign)

    motorSpeedSign = label.Label(
                        smallFont,
                        text='{:.0f}'.format(mSpeed),
                        color=0xFFFFFF,
                        scale=TEXT_SCALE,
                        anchor_point=(0, .5),
                        anchored_position=(90, 360)
                    )
    motorDirSign.text = flipChar
    motorDirSign.text = initChar
    main.append(motorSpeedSign)

    motorDripLine = Line(160, 340, 160, 380, divLineColor)
    main.append(motorDripLine)



    wSpeed, _ = readMotorSpeedDirection(1)

    waterDirSign = label.Label(
                        symbolFont,
                        text = chr(152),
                        color=0xFFFFFF,
                        scale=TEXT_SCALE,
                        anchor_point=(0.5, 0.5),
                        anchored_position=(200, 360)
                    )
    main.append(waterDirSign)

    waterSpeedSign = label.Label(
                        smallFont,
                        text='{:.0f}'.format(wSpeed),
                        color=0xFFFFFF,
                        scale=TEXT_SCALE,
                        anchor_point=(0, .5),
                        anchored_position=(260, 360)
                    )
    main.append(waterSpeedSign)

    return mDir, pitchText, rollText, mastText, motorSpeedSign, waterSpeedSign, motorDirSign
'''
class encoder(portAddress):

    def __init__(self):
        isConnected = self.connect(portAddress)
        if isConnected:
            self.oldValue = readEncoder(portAddress)
        else:
            raise Error ('port connectdion familed')

        return

    def sendI2C(self.address, payload):
        return

    def readI2C(self.address):

        return bytes

    def bytesToInt(bytes):

        return intValue

    def readEncoder(self, address):

        sendI2C(address)
        bytes = readI2C(address)
        self.value = self.bytesToInt(bytes)

        return self.value


class trimPot(pin, multiplier):

    def __init__(self, pin, multiplier):
        self.pin = pin
        self.multiplier = multiplier

        _ = self.readTrimpot()

    def readTrimpot(self):

        self.rawValue = analogRead(self.pin)
        self.convert()

        return self.value

    def convert(self):
        self.value = self.rawValue*self.multiplier

        return



class Instrument(encoders = [],
                 trimPots = []):

        def __init__(self):
            self.encoderList = encoders
            self.trimPotsList = trimPots
            return

        def attachEncoder(self, enc):
            self.encoderList.append(enc)

        def attatchTrimpot(potPin, multiplier):
            self.trimpotList(potPin, multiplier)


        def read_all(self):

            for i, each in enumerate(self.encoderList):
                each.readEncoder()

            for each in self.trimpotList:
                each.readTrimpot()

        def write_all(self):
            #write these
            for i, each in enumerate(self.encoderList):
                each.write(textfield)

            for each in self.trimpotList:
                each.write(textfield)

        def tic(self):
            self.read_all()
            self.write_all()
'''

dispFont = bitmap_font.load_font(mainFontFile)
gemFont = bitmap_font.load_font(gemFontFile)

display = initDisplay(disp_DC, disp_CS, disp_reset)
splash = drawSplashScreen(display,
                          splashScreenTime = splashTime,
                          useFont = dispFont,
                          gemFont = gemFont)

display.root_group = None

smallFont = bitmap_font.load_font(smallFontFile)
labelFont = bitmap_font.load_font(labelFontFile)
symbolFont = bitmap_font.load_font(symbFontFile)

motorDirFlag, pitchText, rollText, mastText, motorSpeedSign, waterSpeedSign, motorDirSign  = drawMainScreen(display,
                                                                                              labelFont = labelFont,
                                                                                              dispFont = dispFont,
                                                                                              symbolFont = symbolFont,
                                                                                              smallFont = smallFont)
'''
enc1 = encoder(atPort1, pitchText)
enc2 = encoder(atPort2, rollText)
enc3 = encoder(atPort5, mastText)
pot1 = trimPot(atPort3, motorSpeedSign)
pot2 = trimPot(atPort4, waterSpeedSign)

instrument = Instrument(encoders = [end1, enc2, enc3],
                        trimPots = [pot1, pot2])
'''
#time.sleep(1)

x = 0


while True:

    #Instrument.tic()

    # update text
    #pitchText.text = "{:.2f}".format(x)
    #rollText.text = "{:.2f}".format(x)
    mastText.text = "{:.2f}".format(x)

    x = x+1

    if x > 199:
        x = 0
        motorDirFlag = not(motorDirFlag)
        if motorDirFlag:
            motorDirSign.text = chr(66)
        else:
            motorDirSign.text = chr(67)


    #time.sleep(0)
