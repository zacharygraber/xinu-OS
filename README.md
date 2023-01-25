### Operating Systems
##### Indiana University
##### Spring 2022

---

This repository contains source code completed during my coursework at IU Bloomington, specifically extending and implementing programs in Douglas Comer's [Xinu OS](https://github.com/xinu-os/xinu).

## Build instructions

Copy the file compile/Makedefs.EXAMPLE to compile/Makedefs and make appropriate changes if necessary.  Make sure that the correct COMPILER_ROOT, LIBGCC_LOC and CONF_LFLAGS are set.

The PLATFORM variable should be set to one of:

- arm-qemu
- arm-bbb
- x86-qemu
- x86-galileo

