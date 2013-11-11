README
======

For the driver of TrustedSSD to work, it must be run on a modified Linux kernel.
The main goal of the modification to Linux kernel is to pass the session key 
through the I/O stack of kernel, including VFS, buffer cache or direct I/O, 
generic block layer and SCSI subsystem.

Preparation
-----------
As the source code of Linux kernel is too large and the changes to it is very 
limited, this project doesn't include kernel source code. Instead, you should 
download source code and then apply patch to it. The save your modification to 
kernel, you can run diff to make a patch.

Steps
1. Download source code of Linux kernel 3.2.X from kernel.org. We have tested 
against Linux kernel 3.2.52. Other version is 3.2.X series should be ok.
1. Unpack the source code in `<TrustedSSD project>/kernel_patch/linux-<version>`.
1. Copy the source to `<TrustedSSD project>/kernel_patch/linux-<version>.orig`.
1. Apply the patch by running `./make_patch`.

The result is that you get a modifed Linux kernel in `kernel_patch/linux-<version>` 
and the original one in `kernel_patch/linux-<version>.orig`.

Compile & install the new kernel
--------------------------------
To compile and install the modifed kernel, you have to work under folder 
`kernel_patch/linux-<version>`.

If this is your first compiling the kernel, you have to config it by running:
    make oldconfig
    make menuconfig
You don't have to make any modification to the default configuration.

To compile the kernel, do the following command:
    make -j 8

To install the kernel, do the following commands: 
    sudo make modules_install -j 8
    sudo make install -j 8

After successfully running the above commands, you can then reboot and use 
the new kernel.
