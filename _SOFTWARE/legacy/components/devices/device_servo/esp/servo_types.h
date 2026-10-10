#pragma once

// Kept apart from device_servo.h: the enum catalog (//#ref-enum) and the device record reader scan different headers.

//#ref-enum @alias Servo Type
typedef enum servo_type_e {
  SERVO_TYPE_ANGLE = 0,       //@alias Angle servo @description Moves to an angle between the angle limits and holds it
  SERVO_TYPE_CONTINUOUS = 1,  //@alias Continuous (360) servo @description Turns at a speed from -100 to 100 (0 stops) instead of going to an angle
} servo_type_e;
