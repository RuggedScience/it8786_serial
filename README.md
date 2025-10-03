# it8786_serial
Linux kernel module that adds support for higher baud rates (> 115200) on IT8786 Super IO chips.

# Baud Rates
This module supports all of the standard baud rates up to 115200. After that only the below baud rates are supported

* 128000
* 230400
* 256000
* 460800

***NOTE: The Super IO chip also supports 921600 baud but currently there is an issue in the module at that speed.***

# Compiling
Since this module is built against the Linux kernel, it requires the appropriate headers for the current kernel. This also means that it will need to be recompiled if the kernel is updated. Below is a list of steps for compiling and installing this module for the current kernel.

## Dependencies
The commands to install the dependencies varies between distros. Below are instructions for some common distros.
<details open>
<summary><h5>Debian / Ubuntu</h5></summary>

* `sudo apt update`
* `sudo apt install git build-essential linux-headers-$(uname -r)`
</details>

<details open>
<summary><h5>Fedora</h5></summary>

* `sudo dnf install git kernel-devel`
</details>

<details open>
<summary><h5>CentOS</h5></summary>

* `sudo yum install git gcc "kernel-devel-uname-r = $(uname -r)"`
</details>

After the dependencies are installed the source files can be downloaded and compiled.
* `git clone https://github.com/ruggedscience/it8786_serial.git`
* `make`

If everything goes well the module should be built and there should be an ***it8786_serial.ko*** file in the current directory. The module can be loaded in the current state using `sudo insmod it8786_serial.ko`. To verify the module successfully loaded run `lsmod | grep it8786_serial` and see if it is listed. To remove the module run `sudo rmmod it8786_serial`. This approach is good for testing but not ideal for long term use as the module can't be automatically loaded during boot.

# Installation
To install the module into the default system module location run `sudo make install`. There may be some errors about SSL but this is normal.  
*For more information about signing the module see the [Kernel Docs](https://www.kernel.org/doc/html/v4.15/admin-guide/module-signing.html).*

Once the module has been installed it can be loaded using `sudo modprobe it8786_serial`. Again, this is temporary and won't persist between boots. To make it permenant run `sudo echo "it8786_serial" >> /etc/modules-load.d/it8786_serial.conf` or manually add `it8786_serial` to the `/etc/modules` file.

# Troublehsooting
If there are errors loading the module or opening the COM ports, check the output of `sudo dmesg | grep it8786_serial`. There is also a `DEBUG` flag in the `it8786_serial.c` file that can be set to enable debug logging. After setting that flag the module will need to be recompiled and loaded. Below are the steps to temporarily load a debug enabled module.

* Uncomment the `#define DEBUG` line. (Should be line 11 in *it8786_serial.c*)
* `make`
* `sudo rmmod it8786_serial`
* `sudo insmod it8786_serial.ko`

After the debug module is loaded, make sure to reproduce the failure and then run `sudo dmesg | grep it8786_serial` to get the logs around when the failure happens.
