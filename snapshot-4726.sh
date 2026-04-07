#!/bin/sh
echo "=== SNAPSHOT ===" 
date
./hdmi_probe 2>&1
./clk_diag 2>&1
./gpio_all 2>&1

