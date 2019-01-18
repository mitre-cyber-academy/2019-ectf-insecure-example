# 2019 Collegiate eCTF Example Code

This repository contains an example reference system for MITRE's 2019 [Embedded System CTF](http://mitrecyberacademy.org/competitions/embedded/). 
The example reference system was based off of [this repository](https://github.com/Digilent/Petalinux-Arty-Z7-10).

The example meets all the requirements outlined in the challenge writeup document, but is **not** implemented securely.

## Disclaimer
This code is incomplete, insecure, and does not meet MITRE standards for quality.
This code is being provided for educational purposes to serve as a simple example that meets the minimum functional requirements for the 2019 MITRE eCTF competition.
Use this code at your own risk!

## Getting Started

### Setting up the Development Environment

In order to manage dependencies and allow for easier cross-platform development, a VM provisioned by [Vagrant](https://vagrantup.com) will be used for building and provisioning the design.
You may develop in another environment, however the steps outlined in the build process **must** produce a working boot image.

**On the Arty Z7 board, move jumper JP4 to the two pins labeled 'SD'**

After you have finished initializing the development environment (see the [2019-ectf-vagrant repo](https://github.com/mitre-cyber-academy/2019-ectf-vagrant)), this repository will be cloned to `/home/vagrant/MES` in the virtual machine.
Please refer to the vagrant repository for further setup instructions.

## 1. Overview

The reference design is divided into the following three top-level directories containing the following components:

	/Arty-Z7-10 ~ containing a Xilinx PetaLinux Project
		- Second Stage Bootloader (U-Boot)
		- Kernel
		- FileSystem
			- Mesh Game Loader
		- Device Tree
	/Arty-Z7-10-hardware ~ Submodule to the [2019-ectf-hardware repo](https://github.com/mitre-cyber-academy/2019-ectf-hardware) which contains the Vivado Hardware Project
		- Hardware
	/tools:
		- provisionSystem.py: used to build system images
		- provisionGames.py: used to package games for use in your system
		- packageSystem.py: used to create the boot image
		- deploySystem.py: used to partition and format the SD as well as deploy the boot image


## 2. Provisioning a System

This section assumes that you have followed the instructions for setting up the development environment and PetaLinux tools is installed to `/opt/pkg/petalinux`.

The following process describes how *all* submitted systems will be provisioned.
If your design does not conform to this standard, or does not build using these steps, the design will be considered incomplete.
The inputs and outputs for each tool described in this section are required.
The specific implementation details of the reference design are included in Section 3 of this README.

### provisionSystem.py

To begin, `cd` into the `tools` directory and run the `provisionSystem.py` script with the appropriate arguments.

```bash
cd tools
python3 provisionSystem.py path_to/users.txt path_to/default.txt
```

For example, to provision the reference implementation
```bash
cd tools
python3 provisionSystem.py demo_files/demo_users.txt demo_files/demo_default.txt
```

`provisionSystem.py` must read a users file which is in a MITRE-defined format.
The file contains the usernames and pins of accounts that must be provisioned into the system.
The format is as follows:

`Users.txt`:

	<user1> <pin1>
	<user2> <pin2>
		   .
		   .
		   .
	<userN> <pinN>

`provisionSystem.py` must also read in a file that contains information about games and versions that must be installed on the board by default upon boot.
If this process fails, the board should not boot.
This file takes the following format:
`default.txt`:

    <game1> <version>
    <game2> <version>
           .
           .
           .
    <gameN> <version>

This script will create the following files:
- `SystemImage.bif`: used by Xilinx BootGen to create a boot image
- `FactorySecrets.txt`: any secrets that you want to be generated at build time and passed to the provisioning stage.

For example, the reference implementation generates the following files

`SystemImage.bif`:

	SystemImage: {
        [bootloader] /home/vagrant/MES/tools/files/zynq_fsbl.elf
		${PROOT}/images/linux/Arty_Z7_10_wrapper.bit
		${PROOT}/images/linux/u-boot.elf
		[load = 0x10000000]${PROOT}/images/linux/image.ub
	}

`FactorySecrets.txt`:

	Empty for reference design, but can contain information in a student defined format.

This script will also build the petalinux system components (U-Boot, Kernel, and FileSystem). The resulting images can be found in `Arty-Z7-10/images/linux`.
Under the `hood`, `provisionSystem` runs these commands to build the system:

```bash
cd Arty-Z7-10/
petalinuxenv # loads the petalinux-tools
petalinux-build -x distclean # clean the build so all changes are rebuilt
petalinux-build
```

**IMPORTANT NOTE:** During a first build, the system will download many different source code tarballs from various upstream projects.
This can take a while, particularly if your network connection is slow.
The `downloads` folder is located in `Arty-Z7-10/build/downloads`.
This directory is safe to share between multiple builds on the same machine.
If you clean the build environment with `petalinux-build -x mrproper` it will delete the build folder containing `downloads`. Moving
the `downloads` out of `build` temporarily can save you time when rebuilding.

**IMPORTANT NOTE:** BitBake, the underlying build tool used by petalinux, has the capability to accelerate builds based on previously built output.
This is done using "shared state" files, which can be thought of as cache objects. The sstate-cache is located in `Arty-Z7-10/build/sstate-cache`.
If you clean the build environment with `petalinux-build -x mrproper` it will delete the build folder containing the `sstate-cache`. Moving
the `sstate-cache` out of `build` temporarily can save you time when rebuilding.

## provisionGames.py

The `provisionGames.py` must read a games file which is in a MITRE-defined format.
The file contains the metadata about the games which must be provisioned into the system.

The format is as follows:
`Games.txt`:

	<gamePath1> <gameName1> <gameVersion> <user1> [...<user1>]
	<gamePath2> <gameName2> <gameVersion> <user2> [...<user2>]
			.
			.
			.
	<gamePathN> <gameNameN> <gameVersion> <userN> [...<userN>]

This script will output a game file for each line in the games file to the `games` directory.

`games`
	- `<gameName1>-v<gameVersion>.bin`
	- `<gameName2>-v<gameVersion>.bin`
		.
		.
		.
	- `<gameNameN>-v<gameVersion>.bin`

To provision the games, run the following command with the appropriate argument

```bash
	cd tools
	python3 provisionGames.py path/to/FactorySecrets.txt path/to/games.txt
```

For example, for the reference implementation, the games can be provisioned using the demo files with

```bash
	cd tools
	python3 provisionGames.py files/generated/FactorySecrets.txt demo_files/demo_games.txt
```

## packageSystem.py

Before deploying the system, you must package the images build in `provisionSystem.py` into a petalinux boot image. This is done using the `bootgen` command.

The `packageSystem.py` script runs the `bootgen` command with the `SystemImage.bif` to create a boot image called `MES.bin`.

This script takes one positional argument, the path to the `SystemImage.bif` file.
If you are using this build process, `provisionSystem.py` will generate a bif file in `files/generated/SystemImage.bif`.

To package the system, run the following command with the appropriate argument

```bash
cd tools
python3 packageSystem.py path/to/SystemImage.bif
```

For example, for the reference implementation, the system can be packaged with
```bash
cd tools
python3 packageSystem.py files/generated/SystemImage.bif
```

## deploySystem.py

To deploy the system onto an attached SD card use the `deploySystem.py` script.

The `deploySystem.py` script:
- partitions and formats an attached SD card (optional argument)
- copy the boot image, as `BOOT.bin` onto the FAT partition of the SD card
- if specified, copy the `MES.bin` image onto the FAT partition of the SD card (see below for more clarification)
- copy games in the `games` folder onto the Ext4 partition of the SD card

MITRE will be providing each team with a `BOOT.bin` file that should be placed into `files/BOOT.bin`.
This path is expected to be provided as the `boot_path` argument, and the generated `MES.bin` is expected to be passed as the `mes_path` argument.
In the event that your team decided to purchase additional hardware, the provided `BOOT.bin` will **NOT** work.
However, you can provided `MES.bin` as the `boot_path` argument and not specify a `mes_path`.
Note that this flexibility is **not** required for the design of the `deploySystem.py` script.

If you are using this build process, the games will be located in `files/generated/games`.
The `BOOT.bin` will be located at `files/BOOT.bin` (note that you will have to put this here!) and the `MES.bin` will be located at `files/generated/MES.bin`.

**WARNING: the following will format whatever device is specified. Ensure that you are specifying the SD card**
The SD card will be of size approximately 8 GB.

To deploy the system, run the following command with the appropriate arguments
```bash
	cd tools
    python3 deploySystem.py /dev/sdX path/to/BOOT.bin path/to/MES.bin path/to/games/
```

For example, for the reference implementation, the system can be deployed with the output from the previous steps with the following (replacing `/dev/sdX` with the device of the SD card).

```bash
	cd tools
	python3 deploySystem.py /dev/sdX files/BOOT.bin files/generated/MES.bin files/generated/games
```

## Summary

In summary, the organizers will build your system using the following procedure:

	1. create a `Users.txt` file
    2. create a `Default.txt` file
	2. run `python3 provisionSystem.py Users.txt Default.txt`
	3. create a `Games.txt` file
	4. run `python3 provisionGames.py files/generated/FactorySecrets.txt Games.txt`
	5. run `python3 packageSystem.py files/generated/SystemImage.bif`
	6. insert SD card into computer
	5. run `python3 deploySystem.py /dev/sdb files/BOOT.bin files/generated/MES.bin files/generated/games`
	6. insert SD into board
	7. boot

## 3. Modifying the Programmable Logic

Modifying the hardware design is not required for this competition; however, if you choose to, it is recommended that any changes you make are simply modifications to the reference [base design](https://github.com/mitre-cyber-academy/2019-ectf-hardware).

## 4. Reference Design Implementation Details

### provisionSystem.py

During the system provisioning process, `provisionSystem.py` transforms the `Users.txt` and `default_games.txt` file into C header files, which are included in the MITRE Entertainment SHell.
This provides access to the users and pins which are allowed to login to the system, as well as default game requirements.
This script also generates a `bif` file to specify boot information, as well as an empty `FactorySecrets` file.
The `provisionSystem.py` script builds U-Boot, the Kernel, the Device Tree, and the FileSystem (INITRAMFS).
Each of these components is described in greater detail below.

In order to boot the kernel from memory at 0x10000000 we specify where to load the image in the SystemImage.bif. This configuration is set using the load option.

 `[load 0x10000000] path/to/image.ub`.

This line tells the board to load `image.ub` into ram at location `0x10000000` when the board boots.

### provisionGames.py

During the game provisioning process, `provisionGames.py` takes each entry in `Games.txt` and creates a binary with the following format:

`<gameName>-v<gameVersion>`

	version:<gameVersion>\nname:<gameName>\nusers:<user1> <user2>\n<gamebinary>

For example, for a 2048 game, the file would look as follows
`2048-v1.0`

	version:1.0\nname:2048users:demo\n\x90\x23\x23...

### packageSystem.py

During the deployment process, `packageSystem.py` will open the bif file generated in the System Provisioning phase and use it while calling the `bootgen` command.
This will build the `MES.bin` binary.

### deploySystem.py

During the deployment process, `deploySystem.py` will mount the SD card and copy the appropriate boot files onto the SD card.
Finally, the script copies any provisioned games onto the SD card.

### Petalinux Game Loader

In order to play a game, petalinux loads the game binary from a specified location in flash.
There are 2 main files that contain the code for this process.
One is the source code for the C program that loads the game from RAM and writes it to flash.
The second is the startup script that prevents the user from accessing the petalinux shell.
You may want to change this in your final design.
As it is now, the game loader is functional, but simple.

#### main.c

Location: `/ectf-collegiate/MES/Arty-Z7-10/project-spec/meta-user/recipes-apps/mesh-game-loader/files/main.c`

This file loads the game from flash at location `0x1FC00000`. It first reads the size of the game as a 4 byte integer from `0x1FC00000` then reads this length of bytes at location `0x1FC00040`. It then parses out the first 3 metadata lines appended to the beginning of the game binary and writes the remaining bytes to a file. This creates a file in petalinux that is an executable binary.

#### startup.sh
Location: `/ectf-collegiate/MES/Arty-Z7-10/project-spec/meta-user/recipes-apps/mesh-game-loader/files/startup.sh`

This file is specified as a startup script in the `mesh-game-loader/mesh-game-loader.bb` file, therefore it runs when petalinux boots up. It then creates a file for the game to be written, calls mesh-game-loader to write the game binary into the file, gives this file executable permissions, configures the serial device, and then runs the game. After the game executes, the startup script triggers a restart, preventing the user from falling through to the bash prompt.

It is important to note that configuring the serial device must be done BEFORE playing the game. As the rules specify, you must have a serial device accessible to the game that is configured at a baud rate of 115200. This is used for debugging from within the game and for automated testing. If the baud rate is not set to 115200, we will not be able to see the serial output from the game and you will not have a valid design.

### U-Boot and MESH Details

The reference design implements MITRE Entertainment SHell in the Second Stage Bootloader (U-Boot).
The typical U-Boot shell has been replaced with a CLI which supports the command described in the rules and requirements documents.
The details of how each command is implemented are described below to give you an idea of how to use the features of the Arty-Z7.

#### Game Install Table

Installed games and the associated user information are tracked in flash memory.
This is done via a table in which each row contains a flag and depending on the value of the flag, a game name, and the user that the game is installed for.
The row is a struct defined in `include/mesh.h`.

The flag can have 3 values.

`0x00` - A game was installed in this row but it is now uninstalled.
`0x01` - A game is currently installed.
`0xFF` - You are at the end of the table and there are no guarantees about the data found after this location.

The game table must be valid for the commands below to work.
A valid table is defined as one that starts at flash address `0x044`, is made up of a contiguous series of row structs (`games_tbl_row`), in which the last row has the flag `0xFF`.
This is achieved by using a sentinel to determine if the table is initialized.
This sentinel is a random 4 byte value written at flash address `0x40`.
If the sentinel value is found at `0x40` then the table is initialized. If it is not, then MESH writes the sentinel at `0x40` and writes the end of table flag at `0x44`.
The install and uninstall commands are required maintain this invariant whenever they operate on the table.

#### help

Usage: `help`

This command lists all implemented commands in the MeSH shell.
This is accomplished by looping through the `builtin_str` array defined in `include/mesh.h` and printing it to stdout.

#### shutdown

Usage: `shutdown`

This command logs out the user and then shuts down the MeSH shell.
It does not shut down the Arty Z7, it simply breaks out of the command loop found in `common/mesh.c:mesh_loop()`.

#### login

While not a command per say, MeSH prompts for a user and pin upon boot.
The provided username and pin are compared to values in the generated `mesh_users.h` file to ensure that a valid username and password were entered.

#### logout

Usage: `logout`

This command logs out the user and brings you back to the login prompt, allowing another user to log in.
This is done by clearing the user struct with 0's, exiting the command loop, and calling the `mesh_login` function again.

#### list

Usage: `list`

The `list` command lists all games installed for the currently logged in user.
This command reads the MeSH installed games table row by row until it reaches the end of table flag.
Each game is printed where the logged in user is equivalent to the name in the `User` struct.
See MeSH Install Table for more details on how the MeSH installed games table is implemented in flash.

#### play

Usage: `play INSTALLED_GAME`

Arguments

	INSTALLED_GAME		The name of an installed game to play.

This command launches the specified game.
To do this, it first reads the specified game into RAM at address `0x1fc00040`.
This is a reserved region in memory where the Linux Kernel expects there to be a game.

Once this is loaded into RAM, it writes the size of the binary in bytes to RAM address `0x1fc00000`.
This is a reserved region in memory where the Linux Kernel expects the size of the game binary to be.

Finally, it boots the Linux Kernel from RAM at address `0x10000000`.
The linux kernel is loaded into RAM at this address when the Arty Z7 is booted.
This offset is specified in the bif on line 6 that is generated by provisionSystem.py.

Control is then passed to the kernel.

#### query

Usage: `query`

This command queries the ext4 partition of the SD card for all games and prints the name of each to stdout.
This is done using the `mesh_query_ext4` function.
This function is derived from the hush shell `ext4fs_ls` function provided by u-boot.
The `mesh_query_ext4` function is a standalone function that sets the read device to the second partition on the sd card and
then lists each regular file in the root of that partition.

It is assumed that the only regular files in the games partition are actually games.

#### install

Usage: `install GAME_NAME`

Arguments

	GAME_NAME		The name of a game located on the games partition of the
					sd card to install.

This command installs the specified game name for the current user.
The game must be in the games partition on the SD card.

This command is implemented very similarly to the `list` command by looping through each row in the game table.
However, when either the uninstalled game flag (`0x00`) or the end of table flag (`0xff`) is located, it writes the table row struct to that location in memory.

If the game is at the end of the table, then it updates the next row to have the end of table flag (`0xff`).

See MeSH Install Table for more details on how the MeSH installed games table is implemented in flash.

#### uninstall

Usage: `uninstall GAME_NAME`

Arguments

	GAME_NAME		The name of an installed game to uninstall.

This command uninstalls the specified game name for the logged in user.
The game must be in the games partition on the SD card.

This command is implemented very similarly to the `list` command by looping through each row in the game table.
If the installed game flag is found and the game name and user match, it clears that row to all 1's and sets the flag to `0x00`, signifying an uninstalled game.

See MeSH Install Table for more details on how the MeSH installed games table is implemented in flash.

#### dump

Usage: `dump offset N`

Arguments
	N 			The number of bytes to print to stdout (in hex)
	offset		The offset to start reading from (in hex).

This command is provided for purposes of testing and is not required for the shell.
It provides an easy way to view data written to flash.
This function utilizes u-boot shell commands, however, due to these commands being static functions, it prepares the arguments as a string (to mimic being called from the command line). It is also important to note that this command interprets `offset` and `N` as hexidecimal numbers. 

#### resetflash

Usage: `resetflash`

This command is provided for purposes of testing and is not required for the shell.

It provides an easy way to reset the entire flash. Under the covers, it runs sf erase 0 0x1000000 which erases the first 0x1000000 bytes. The flash is 16 MB == 16MB * 1024KB/MB * 1024B/KB == 16777216B == 0x1000000 bytes.

It is important to note that when erasing flash, you can only clear it by pages. A page is 64KB, therefore you must clear at least 0x10000 bytes at a time. Furthermore, the sf erase function only works on page boundaries and will give you an error if done with an offset at any other point.

### Device Tree

U-Boot is responsible for loading the game binary into a reserved region in RAM. The reference design reserves a memory region by adding the following node to the device tree:

_Arty-Z7-10/project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dts:_

	...
	memory {
		device_type = "memory";
		reg = <0x00000000 0x1fc00000>;
	};

	reserved_memory {
		device_type = "reserved_memory";
		reg = <0x1fc00000 0x00400000>;
	};
	...

### Linux

Linux is responsible for reading the reserved memory region and launching the game. A petalinux init app is used to load and launch the game and can be found here:

_Arty-Z7-10/project-spec/meta-user/recipes-apps/mesh-game-loader_

### FileSystem

## Building the Reference Design Instructions

In summary, to build the reference design for the first time, follow the steps below:
1. Ensure that all steps to provision the development environment were completed as listed in the **Provision Instructions** section in the 2019-ectf-vagrant README
2. Log in to the newly provisioned vagrant machine using the credentials `vagrant:vagrant`
3. Open a terminal and cd to the MES tools directory: `cd ~/MES/tools`
4. Provision the system: `python3 provisionSystem.py demo_files/demo_users.txt demo_files/demo_default.txt`
5. Provision the games: `python3 provisionGames.py files/generated/FactorySecrets.txt demo_files/demo_games.txt`
6. Package the system: `python3 packageSystem.py files/generated/SystemImage.bif`
7. Follow the steps in section **Setting Up USB Passthrough** in the 2019-ectf-vagrant README to passthrough the SD card and Xilinx board
8. Insert the SD card into the provided adapter
9. Deploy the system: `python3 deploySystem.py /dev/sdX files/BOOT.bin files/generated/MES.bin files/generated/games` (replacing `/dev/sdX` with the appropriate device)
10. Remove the SD card and place it in the board
11. On the Arty Z7 board, move jumper JP4 to the two pins labeled 'SD'
12. Follow the **Accessing UART From Inside the VM** section in the 2019-ectf-vagrant README to connect to the Xilinx board from within the VM
13. Press the `POBR` button on the board to reset it. You should now see the mesh shell boot and will be greeted with the login prompt
14. Log in with the demo credentials `demo:00000000`


## Helpful Build Tips

Petalinux takes an INCREDIBLY long time to build the whole project. When you are developing, it may be useful to use this little trick we came up with to build using the underlying makefile that petalinux-build uses. We figured out how to do this to build u-boot, but it could most likely be used to compile other parts of the project.

NOTE: This is a workaround for speeding up the offical build process. You may encounter some errors when switching back and forth from using make and using petalinux-build. If you do not have a completely clean directory, petalinux-build fails with weird and useless errors. 

### Building U-Boot Using _make_

The process below builds the u-boot image using make, then copies it into the images folder that petalinux-build uses for the compiled images. It then skips the provisionSystem script and JUST runs deploySystem so that this new build image will be deployed to the SD card.

1. Add the device tree compiler (`dtc`) to your path: `export PATH=$PATH:/home/vagrant/MES/Arty-Z7-10/build/tmp/sysroots/x86_64-linux/usr/bin/`
2. Change directory to u-boot: `cd /home/vagrant/MES/Arty-Z7-10/components/ext_sources/u-boot-ectf` 
3. Run the make command with some environment variables set: `ARCH=arm CROSS_COMPILE=/opt/pkg/petalinux/tools/linux-i386/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf- make zynq_ectf_defconfig && ARCH=arm CROSS_COMPILE=/opt/pkg/petalinux/tools/linux-i386/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf- make`
4. Copy the build image to the petalinux images folder: `cp ./u-boot /home/vagrant/MES/Arty-Z7-10/images/linux/u-boot.elf`
5. Build `MES.bin` using `packageSystem.py` and deploy using `deploySystem.py`

### Cleaning the U-Boot Directory

After using `make`, you will be unable to build with `petalinux-build` until you clean out the directory.
After you complete the following steps, you should be able to build using `petalinux-build` again.
If for some reason you can't, clean the repo and you should be good, so use good version control practices.
Just make sure you don't commit any of the files make creates!

1. Change directory to u-boot. `cd /home/vagrant/MES/Arty-Z7-10/components/ext_sources/u-boot-ectf`
2. Run the `make` command for `mrproper`. This will get rid of all the make files that were created in the directory. `make mrproper`

## 5. References
XAPP1175: Secure Boot of Zynq-7000 All Programmable SoC:
https://www.xilinx.com/support/documentation/application_notes/xapp1175_zynq_secure_boot.pdf

UG821: Zynq-7000 All Programmable SoC Software Developers Guide:
https://www.xilinx.com/support/documentation/user_guides/ug821-zynq-7000-swdev.pdf

UG1144: PetaLinux Tools Documentation Reference Guide:
https://www.xilinx.com/support/documentation/sw_manuals/xilinx2017_1/ug1144-petalinux-tools-reference-guide.pdf

UG1156: Petalinux Tools Documentation Workflow Tutorial:
https://www.xilinx.com/support/documentation/sw_manuals/xilinx2017_3/ug1156-petalinux-tools-workflow-tutorial.pdf
