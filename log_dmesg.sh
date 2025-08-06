#!/bin/bash
LOG_DIR="/var/log/dmesg_logs"
mkdir -p "$LOG_DIR"
dmesg > "$LOG_DIR/dmesg_$(date +%Y%m%d_%H%M%S).log"
