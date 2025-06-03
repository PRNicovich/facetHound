# Write your code here :-)

def readKeyboard(uart, nBytes = None, magicByte = 14, returnRaw = False):

    data = uart.readline()



    #if not(data is None):
    #    out = data[magicByte]
    #else:
    #    out = None

    # Catch error with \n in middle of payload
    # Should always be \xFE [20ish bytes] \x00\n
    # False \n resolves to 7/j

    if (len(data) != 21):
        data = data + uart.readline()

    out = None
    if returnRaw:
        return out, data
    else:
        return out


def keyRouter(key):

# Include error correction on knob chars


    if key == 4:
        out = 'a'
    elif key == 5:
        out = 'b'
    elif(key == 6):
        out = 'c'
    elif(key == 7):
        out = 'd'
    elif key == 8:
        out = 'e'
    elif(key == 9):
        out = 'f'
    elif(key == 10):
        out = 'g'
    elif key == 11:
        out = 'h'
    elif(key == 12):
        out = 'i'

    elif(key == 30):
        out = '1 <'
    elif key == 31:
        out = '1 .'
    elif(key == 32):
        out = '1 >'

    elif(key == 33):
        out = '2 <'
    elif key == 34:
        out = '2 .'
    elif(key == 35):
        out = '2 >'

    elif(key == 36):
        out = '3 <'
    elif key == 37:
        out = '3 .'
    elif(key == 38):
        out = '3 >'

    else:
        out = None

    return out

