# Kali NetHunter Kernel Builder

<!--![Kali NetHunter](https://gitlab.com/kalilinux/nethunter/build-scripts/kali-nethunter-project/raw/master/images/nethunter-git-logo.png)-->

## Installation

Clone this repository into your kernel source tree, e.g.

``` bash
cd android_kernel_oneplus_sm8150/
git clone https://gitlab.com/kalilinux/nethunter/build-scripts/kali-nethunter-kernel
```

**cd** into `kali-nethunter-kernel/`, open `config` and make sure that you are happy with all the settings.

Important: Changes should not be made in this file. Copy it accross to `local.config` and delete everything except the parameters you would like to change. Change those parameters and save it.

The settings in `local.config` overwrites `config` but will itself not be overwritten by updates.


Mon Aug 16 11:20:01 UTC 2021
