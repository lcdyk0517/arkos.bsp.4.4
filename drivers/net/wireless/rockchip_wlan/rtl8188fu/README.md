# rtl8188fu-20230217

RTL8188FU/RTL8188FTV driver for FPV. 
~~For archive only.~~  
~~No plan for FPV, as RTL873xBU is a better low-end choice.~~   

Download pre-built OpenIPC firmware with this driver integrated [here](https://github.com/libc0607/openipc-firmware/releases/tag/latest) to help test.  

Original driver: [RTL8188FU_Linux_v5.15.3-6-g1a2e952f9.20230217.tar](https://github.com/libc0607/rtl8188fu-20230217/blob/main/RTL8188FU_Linux_v5.15.3-6-g1a2e952f9.20230217.tar)  

Schematic: [SCH-UM88F02-7V0.pdf](https://github.com/libc0607/rtl8188fu-20230217/blob/main/SCH-UM88F02-7V0.pdf)  
PCB (Open it with PADS): [PCB-UM88F02-7V0.pcb](https://github.com/libc0607/rtl8188fu-20230217/blob/main/PCB-UM88F02-7V0.pcb)  
*** All collected from the Internet. Use it at your own risk & contact me to delete if you mind. ***  

Please do not choose this adapter for FPV unless you have less than a $0.5 budget. (  

Tested:
 - Build on kernel 6.8  
 - TX power unlocked (same as the 8812eu driver), but don't set it too high  
 - Monitor & injection, 802.11b(CCK)/g/n rates all works well

Need test or can be further investigated:
 - 5M/10M bandwidth, only tested on a spectrum analyzer and the spectrum seems good

Not working:
 - 40MHz bandwidth on RTL8188FTV
 - STBC/LDPC not supported by hardware
 - ...

To-Do: 
 - EDCCA patch
 - MTU
 - Test with OpenIPC

### Why I'm doing this
Someone: “RTL8731BU (~$2.5) is too expensive (for FPV). Any choice at $0.3 ~ $0.4?”  

![1c98756a56f4cb5f00966225649a731](https://github.com/user-attachments/assets/39b78b8f-382c-4c48-96c8-d16d35394e01)


