# Facet Hound
Design and software files for the Facet Hound digital lapidary faceting machine.

The Facet Hound a mast-style faceting machine with digital readouts on all three axes, electronic indexing precise to less than 0.01 degrees, and direct-driven lap with brushless DC motor. It's designed to be easily replicated, with readily-available components and a minimum of custom parts.  In 2025 the total cost for included components is ~$1600.  

Complete design is included in ./Hardware/FaceHoundv7.step.  Sub-folders of ./Hardware/ contain files to share with vendors for machining (.step, .pdf), 3D printing (.stl), PCB fab (.brd), and laser/plasma/waterjet cutting (.dxf).  Commercially available parts are called out in main design.   

Sub-folders of ./Software/ are the sketch files for each of the four sub-modules.  The main module is a Raspberry Pi Pico 2 and peripherals are RP2040 Zero clones.  All are programmed in C using the Arduino IDE.  All necessary dependencies are straightforward to install through the IDE. 

These are preliminary designs that may include errors.  Feel free to contribute your own modifications or bug fixes as pull requests. 
