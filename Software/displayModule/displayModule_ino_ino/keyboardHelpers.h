
// Helper function to print key names
void printKeyName(uint8_t key) {
  // This is a simplified list. Full HID keyboard has many more key codes
  switch (key) {
    case 0x04: Serial.print("A"); break;
    case 0x05: Serial.print("B"); break;
    case 0x06: Serial.print("C"); break;
    case 0x07: Serial.print("D"); break;
    case 0x08: Serial.print("E"); break;
    case 0x09: Serial.print("F"); break;
    case 0x0A: Serial.print("G"); break;
    case 0x0B: Serial.print("H"); break;
    case 0x0C: Serial.print("I"); break;
    case 0x0D: Serial.print("J"); break;
    case 0x0E: Serial.print("K"); break;
    case 0x0F: Serial.print("L"); break;
    case 0x10: Serial.print("M"); break;
    case 0x11: Serial.print("N"); break;
    case 0x12: Serial.print("O"); break;
    case 0x13: Serial.print("P"); break;
    case 0x14: Serial.print("Q"); break;
    case 0x15: Serial.print("R"); break;
    case 0x16: Serial.print("S"); break;
    case 0x17: Serial.print("T"); break;
    case 0x18: Serial.print("U"); break;
    case 0x19: Serial.print("V"); break;
    case 0x1A: Serial.print("W"); break;
    case 0x1B: Serial.print("X"); break;
    case 0x1C: Serial.print("Y"); break;
    case 0x1D: Serial.print("Z"); break;
    case 0x1E: Serial.print("1"); break;
    case 0x1F: Serial.print("2"); break;
    case 0x20: Serial.print("3"); break;
    case 0x21: Serial.print("4"); break;
    case 0x22: Serial.print("5"); break;
    case 0x23: Serial.print("6"); break;
    case 0x24: Serial.print("7"); break;
    case 0x25: Serial.print("8"); break;
    case 0x26: Serial.print("9"); break;
    case 0x27: Serial.print("0"); break;
    case 0x28: Serial.print("ENTER"); break;
    case 0x29: Serial.print("ESC"); break;
    case 0x2A: Serial.print("BACKSPACE"); break;
    case 0x2B: Serial.print("TAB"); break;
    case 0x2C: Serial.print("SPACE"); break;
    case 0x2D: Serial.print("MINUS"); break;
    case 0x2E: Serial.print("EQUAL"); break;
    case 0x2F: Serial.print("LBRACKET"); break;
    case 0x30: Serial.print("RBRACKET"); break;
    case 0x31: Serial.print("BACKSLASH"); break;
    case 0x33: Serial.print("SEMICOLON"); break;
    case 0x34: Serial.print("QUOTE"); break;
    case 0x35: Serial.print("GRAVE"); break;
    case 0x36: Serial.print("COMMA"); break;
    case 0x37: Serial.print("PERIOD"); break;
    case 0x38: Serial.print("SLASH"); break;
    case 0x39: Serial.print("CAPS_LOCK"); break;
    case 0x4F: Serial.print("RIGHT_ARROW"); break;
    case 0x50: Serial.print("LEFT_ARROW"); break;
    case 0x51: Serial.print("DOWN_ARROW"); break;
    case 0x52: Serial.print("UP_ARROW"); break;
    default:
      if (key >= 0x3A && key <= 0x45) { // F1-F12
        Serial.print("F");
        Serial.print(key - 0x3A + 1);
      } else {
        // For keys not handled above, just print the HID code
        Serial.print("0x");
        Serial.print(key, HEX);
      }
      break;
  }
}

void printKeyboardReport(uint8_t const* report) {
  // First byte contains modifier keys
  uint8_t modifiers = report[0];
  
  // Print modifier keys if pressed
  if (modifiers > 0) {
    Serial.print("Modifiers: ");
    
    if (modifiers & 0x01) Serial.print("LEFT_CTRL ");
    if (modifiers & 0x02) Serial.print("LEFT_SHIFT ");
    if (modifiers & 0x04) Serial.print("LEFT_ALT ");
    if (modifiers & 0x08) Serial.print("LEFT_GUI ");
    if (modifiers & 0x10) Serial.print("RIGHT_CTRL ");
    if (modifiers & 0x20) Serial.print("RIGHT_SHIFT ");
    if (modifiers & 0x40) Serial.print("RIGHT_ALT ");
    if (modifiers & 0x80) Serial.print("RIGHT_GUI ");
    
    Serial.println();
  }
  
  // Second byte is reserved (usually 0)
  
  // Bytes 2-7 contain up to 6 key codes
  bool keysPressed = false;
  
  for (int i = 2; i < 8; i++) {
    uint8_t key = report[i];
    
    // Skip if no key or error rollover
    if (key == 0 || key == 1) {
      continue;
    }
    
    if (!keysPressed) {
      Serial.print("Keys: ");
      keysPressed = true;
    }
    
    // Print key name based on HID Usage Tables for standard keyboard
    printKeyName(key);
    Serial.print(" ");
  }
  
  if (keysPressed) {
    Serial.println();
  } else if (modifiers == 0) {
    Serial.println("No keys pressed");
  }
}
