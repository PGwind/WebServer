#!/bin/bash
cd build
make clean
make
cd ..
echo -e "\e[96mTiny Web 正在执行...\e[97m"
./server 9999 /opt