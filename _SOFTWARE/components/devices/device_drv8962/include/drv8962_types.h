#pragma once

// Kept apart from device_drv8962.h: the enum catalog (//#ref-enum) and the device record reader scan different headers.

//#ref-enum @alias DRV8962 Topology
typedef enum drv8962_topology_e {
  DRV8962_TOPOLOGY_2_FULL_BRIDGES = 0, //@alias Two full bridges @description Channel 0 drives OUT1+OUT2 (motor A), channel 1 drives OUT3+OUT4 (motor B), both directions
  DRV8962_TOPOLOGY_4_HALF_BRIDGES = 1, //@alias Four half bridges @description Channels 0..3 drive OUT1..OUT4 one by one (solenoids, single-direction loads)
} drv8962_topology_e;
