#!/bin/bash

cd /home/open/old_deploy/yolov8_multhreading_Airfi/install/rknn_yolov8_demo_Linux

sleep 1

./rknn_yolov8_demo \
  /home/open/old_deploy/yolov8_multhreading_Airfi/install/rknn_yolov8_demo_Linux/model/RK3588/airfi-class-2-50e-28feb.rknn 11 MIPI ../../ed_config.txt \
  2> >(ts '[%Y-%m-%d %H:%M:%S]' >> /var/log/airfi/airfi_error.log) \
  | ts '[%Y-%m-%d %H:%M:%S]' >> /var/log/airfi/airfi.log
