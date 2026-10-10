set confirm off
set pagination off
target remote :1234
break adc_hw_calibration
commands
silent
return
continue
end
continue
