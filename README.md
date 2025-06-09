[![twitter spresense handle][]][twitter spresense badge]
[![twitter devworld handle][]][twitter devworld badge]

# Welcome to SPRESENSE project

Clone this repository and update submodules.

```
$ git clone --recursive git@github.com:EdgeAI-BestPractices/spresense.git
```

# Update bootloader

Correct bootloader is required for using Spresense board properly.

```
cd spresense/sdk
tools/flash.sh -e <<Downloaded zip file>>
tools/flash.sh -l ../firmware/spresense -c /dev/ttyUSB0
```

# kernel settings

Please apply the defconfig that I have already prepared.

```
cd spresense/sdk
tools/config.py defconfig
make
```

# init script

Please flash init.rc using following commands
It will be written to the main board as a startup script.

```
cd spresense/sdk
tools/flash.sh -w init.rc
tools/flash.sh -c /dev/ttyUSB0 nuttx.spk
```

# Submodules

```
spresense                  - This repository
|-- nuttx                  - NuttX original kernel + SPRESENSE port
|-- sdk
|   `-- apps               - NuttX original application + SPRESENSE port
`-- externals
    `-- nnablart
      `-- nnabla-c-runtime - Neural Network Runtime library
```

## Update SDK2.x.x to SDK3.x.x

The URL of the submodule (nuttx, sdk/apps) in the spresense repository has been changed since SDK3.0.0. If you cloned the repository before SDK2.x, please run the following instructions to update the URL of the submodule.

```
$ cd spresense
$ git fetch origin
$ git checkout <remote branch>
$ git submodule sync
$ git submodule update
```
* `<remote branch>`
  * For master branch: `origin/master`
  * For develop branch: `origin/develop`

# Spresense SDK build instructions

Build instructions are documented at [Spresense SDK Getting Started Guide](https://developer.sony.com/develop/spresense/docs/sdk_set_up_en.html).

## Prerequisites

Install the necessary packages and GCC ARM toolchain for cross-compilation.
```
$ wget https://raw.githubusercontent.com/sonydevworld/spresense/master/install-tools.sh
$ bash install-tools.sh
```

## Build

Go to the folder where you cloned the {SDK_FULL}, and enter the `sdk` folder name:
``` bash
$ cd spresense/sdk
```
Set up the SDK configuration
``` bash
$ tools/config.py examples/hello
```
Build the example image:
``` bash
$ make
```

A `nuttx.spk` file appears in the `sdk` folder when this step has successfully finished.
This file is the final result and can be flashed into the your board.

# Using docker

A pre-compiled docker container is available with all the pre-requisite that is needed in order to build the Spresense SDK.

In order to start using it simply type:

```
$ source spresense_env.sh
```

This script will create an alias `spresense` which should proceed the regular SDK build scripts and Make commands.

Examples:
```
SpresenseSDK: $ spresense tools/config.py examples/hello
SpresenseSDK: $ spresense make
```

[twitter spresense handle]: https://img.shields.io/twitter/follow/SpresensebySony?style=social&logo=twitter
[twitter spresense badge]: https://twitter.com/intent/follow?screen_name=SpresensebySony
[twitter devworld handle]: https://img.shields.io/twitter/follow/SonyDevWorld?style=social&logo=twitter
[twitter devworld badge]: https://twitter.com/intent/follow?screen_name=SonyDevWorld
