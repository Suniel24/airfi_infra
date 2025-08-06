#!/bin/bash

mkdir -p /var/log/airfi
touch /var/log/airfi/airfi.log
touch /var/log/airfi/airfi_error.log

chown -R open:open /var/log/airfi
chmod 664 /var/log/airfi/airfi.log
chmod 664 /var/log/airfi/airfi_error.log
