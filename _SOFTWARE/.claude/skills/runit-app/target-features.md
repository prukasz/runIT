Overall targets:
- connect using different technologies - ble, wifi and custom later added connectors 
- Two defferent design levels: basic and advanced - that will hide or override some of elements 
- Mobile and web layout in one (same one)
- Procedural generation of objects like settings, blocks, devices - from provided jsons
- Runtime recreation of blocks and object allowing for live telemetry diplay 
- Display of errors returned by runit-esp - linked to corresponding objects
- Compilation to vm packets along with bad patterns detection and in basic mode - automatic variable type
- Interlinked relation between components with view swich option: Device shows linked blocks -> move to blocks -> blocks shows linked variables -> move to variables: variables shows linked blocks and so on. 
- Interlinked erorr mapping - when error comes and possible to idenify then lit up related device and related block and so on 
- step by step code run (debug view) - and loop by loop and stop  with live data display (from telemetry) and block states - that is important (flow state) 
- Remote control option as separate wiev - linked to objects - and overriden after change
----------------------------------------------------------
Overall views that would like to have: 
- settings page - BLE and log setup as well as app setup - all saved in json to export - look dependant on basic or advanced mode 
- Devices and features page - install test and setup devices - devices, features + linked contracts, One main ruint-board overview that maps connected elements to physical layout svg - similar to stm
- action setup mode record works like: first we setup desired state of devices and add commands then we just upload all under action id. store action code in json.
- Code page with - object tab (user only) (basic and simplifiew view) - with object creation - folder structure -and value asignment 
- Code page with block palette and object palette(tree) with drag n drop on canvas and feature list (crated)
- Block setting when on canvas 
- multiple canvas - logical order is first canvas executes first
two sidebars: left exproarot category selector, view selector
right sidebar (collapsed - run stop and so on options ) and expanded with settings additional 
- remote control view - with pallete of sliders switches displays and so on, along with right bar used for configuration and left sidebar used for pallete 
-----------------------------------------------------------
some more selected details: 
dedicated docs page and info page or window for all components if help provided - like how to use and so on. 
Auto adjusted and collapsing left and right bar size 
2 wievs of blocks on canvas - basic and extended 
canvas with autosnap grid 
marking of repeat loops 
dark and light mode 
console(read only with decoded data stream) - advanced 
mapped confirmation of sent commad and received ack for example per one contract call 
distinguishable summary of what created action does: sandbox mode -ie not send in live 
load code and all settigns from external jsons
export code to external json 






