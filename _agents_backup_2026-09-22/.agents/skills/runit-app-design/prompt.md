Hi i want to prepare this repo for ai usage.
First strip all agent targeteted files apart those from @data-structures.

Now overall sketch so you get the point what this repo is about

- runIT is framework for entry level users of electronics, embedded and remote controll hobbysts
- runIT consist of three conceptual layers:
Hardware , embedded software and phone and pc compatible app:
- runIT aims for plug n play experience
- runIT aims for modularity
- runIT works on the edge of low-level embedded, electronics, proprietary RC systems and IoT.
- framework aims to take best parts of each one:
performance and optional full hardware access (low level)
Integrategration flexibility and safety aimed for electronics handling
- plug n play remote experience of remote control with high abstraction level
- connection flexibility data processing and hardware-word integration of IoT

How its achieved:
    runIT-ESP32 - this is Free-Rtos relying application providing three main things:

Unified hardware interface by providing abstraction layer using contracts:

Unified connectivity layer by enabling real time OTA provided by byte packets stream (interface agnostic) - native support for BLE (main interface) and wifi coexistance (not yet implemented) along with user provided connection methods like LoRa or other 833GHz.. Concept of devices enforces one universal - device agnostic type of API - for hardware access - GPIO and so on. Device driver must adopt this scheme by providing adapters. If reduces dramatically end user exerience complexity dramatically by converting device to just number and unified label along with cross device cooperation on enforced API. System also enforces unified error handling - with nested error chain with full logging and reaction on errors.

High level strictly customized flow type language:

It aims to suite most used, and popular usages. its not all trick pony. Language consist of object - semi static structure with easy to implement json serialization, along with accessors to objects - runtime evaluated. currently object support all necessarty types with full support for nesting and mixed types trees. On top of layers lays blocks - code snippets working on created objects, that expose result in own outputs. design is mix of classical PLC and Node flows like Node-Red. Uniqe functionality is native support for remote controll mode - dual side object data exchage that allows to implement high complexity remote behaviour. In addition language can work fully offline as normal microcontroller. By providing unified contracts for devices - number of blocks and their content can be dramatically reduced reducing design complexity.

High level devices (features) and hardware agnostic design:

As code enforces modularity hardware it runs on can be customizable. Features - using connected devices provide functionality for remote contol hardware like motors, servos, actuators. Features based on provided hardware will also support mostly used functionalities like servo trimming, homing, motor ramp up.

Overall design of software along side real time upload of code with no need of flashing hardware. create perfect enviorment for rapid prototyping for non specialistic usecases.

runIT board is concept fully showing power of this code.
runIT board reduces cable conentions to bare minimum, it posess most of trouble causing components to implement in project. Flexibe psu - USB-C PD, standard lipo battery and fixed psu. with range 5 - 20V.
Onboard protection reduces hardware failure due to user misbehavoiur.
Next key point is onboard voltage regulators with power lines multiplexing, protection - current monitoring and linear voltage set up -remotely along with current limit.
it also isolates ESP32-S3 from external enivorment - by providnign separate internal voltage.
Voltage regulators can power devices and features directly or using onboard upto 100W h bridges with current monitoring.
Onboard upto 20 v adc provides 8 channes to read parameters or act like 20V inputs
Onboard pwm expander exposes 8 channes with 5V suppllied form one of two onboard regulators.
in addition esp 32 pins are also exposed with additional protection and 3.3V supply to provide normal mcu functionalities. All of this reduces hardware need to near final features like servos or motors leds, and custom sensors all powered by one board with exposed screw terminals for duarbility and plug n play goldpins.

runIT app is finalizing project to achieve most ergonomic and user frienfdly interface ready for future changes and expansion. It can act as remote, debugger, configurator and code ide with blocks canvas.

All of this software doesnt limit user to one hardware runit-EPS32 can run on normal devkit and handle even one custom sensor still providing all functionality not regarding devices installed.

Finally end user can never see line of normal code when interacting with project and board. selecting only options and placing nodes on canvas.




