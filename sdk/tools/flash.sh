#!/usr/bin/env bash
############################################################################
# tools/flash.sh
#
#   Copyright 2018, 2025 Sony Semiconductor Solutions Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of Sony Semiconductor Solutions Corporation nor
#    the names of its contributors may be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
############################################################################

CURRENT_DIR=`pwd`
SCRIPT_NAME=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)/$(basename "${BASH_SOURCE[0]}")
SCRIPT_DIR=`dirname "$SCRIPT_NAME"`
BOOTLOADER_ZIP="$SCRIPT_DIR/../../firmware/spresense/firmware.zip"
BOOTLOADER_PATH="$SCRIPT_DIR/../../firmware/spresense"

# function   : show_help
# description: Show help and exit.
function show_help()
{
	echo "  Usage:"
	echo "       $0 [-c <port> -b <baudrate>] [-B] [-w] [<file> ...]"
	echo ""
	echo "  Optional arguments:"
	echo "       -c: Serial port (default: /dev/ttyUSB0)"
	echo "       -b: Serial baudrate (default: 115200)"
	echo "       -B: Install Bootloader"
	echo "       -r: Remove nuttx(Main Core SPK) and other files from spresense board"
	echo "       -w: Worker load mode"
	echo "       -h: Show help (This message)"
	exit
}

# Get platform type
case "$(uname -s)" in
	Linux*)
		PLATFORM=linux
		;;
	Darwin*)
		PLATFORM=macos
		;;
	CYGWIN*|MINGW32*|MSYS*)
		PLATFORM=windows
		;;
	*)
		echo "ERROR: Unknown platform"
		echo ""
		show_help
		;;
esac

# Exec file extension postfix
EXEEXT=""

# Option handler
# -b: UART Baudrate (default: 115200)
# -c: UART Port (default: /dev/ttyUSB0)
# -h: Show Help
UART_BAUDRATE="115200"
UART_PORT="/dev/ttyUSB0"
UPDATE_ZIP=""
LOADR_PATH=""
FLASH_MODE="SPK"
while getopts b:c:s:e:l:Brwh OPT
do
	case $OPT in
		'b' ) UART_BAUDRATE=$OPTARG;;
		'c' ) UART_PORT=$OPTARG;;
		'e' )
			  echo "This option has been deprecated."
			  echo "Please use the following command instead."
			  echo ""
			  echo "$0 -c <port> -B"
			  exit;;
		'B' ) UPDATE_ZIP=$BOOTLOADER_ZIP
			  LOADR_PATH=$BOOTLOADER_PATH;;
		'l' ) LOADR_PATH=$OPTARG;;
		'r' ) FLASH_MODE="REMOVE";;
		'w' ) FLASH_MODE="ELF";;
		'h' ) show_help;;
	esac
done

# Shift argument position after option
shift $(($OPTIND - 1))

if [ "${UPDATE_ZIP}" != "" ]; then
	rm -f ${LOADR_PATH}/*.espk
	rm -f ${LOADR_PATH}/*.spk
	${SCRIPT_DIR}/bootloader.py -i ${UPDATE_ZIP}
	if [ $? -ne 0 ]; then
		exit
	fi
fi

# WSL/WSL2 detection
if [ "${PLATFORM}" == "linux" ]; then
	if [ "$(uname -r | grep -i microsoft)" != "" ]; then
		if [[ "$UART_PORT" == COM* ]]; then
			# WSL/WSL2 is a linux but USB related SDK tools
			# should use windows binary.
			PLATFORM=windows
			EXEEXT=".exe"
		fi
	fi
fi

# Check loader version
${SCRIPT_DIR}/bootloader.py -c

if [ "${FLASH_MODE}" == "SPK" ]; then
	# Pickup spk and espk files
	ESPK_FILES=""
	SPK_FILES=""

	for arg in $@
	do
		if [ "`echo ${arg} | grep "\.spk$"`" ]; then
			SPK_FILES="${SPK_FILES} ${arg}"
		elif [ "`echo ${arg} | grep "\.espk$"`" ]; then
			ESPK_FILES="${ESPK_FILES} ${arg}"
		fi
	done

	if [ "${LOADR_PATH}" != "" ]; then
		ESPK_FILES="${ESPK_FILES} `find ${LOADR_PATH} -name "*.espk"`"
		SPK_FILES="${SPK_FILES} `find ${LOADR_PATH} -name "*.spk"`"
	fi

	if [ "${SPK_FILES}${ESPK_FILES}" == "" ]; then
		echo "ERROR: No (e)spk files are contains."
		echo ""
		show_help
	fi

	# Flash spk files into spresense board
	${SCRIPT_DIR}/${PLATFORM}/flash_writer${EXEEXT} -s -c ${UART_PORT} -d -b ${UART_BAUDRATE} -n ${ESPK_FILES} ${SPK_FILES}
	if [ $? -ne 0 ]; then
		if [ "${UPDATE_ZIP}" != "" ]; then
			# If the flash_writer failed, remove all files unzipped in the loader path
			rm -f ${LOADR_PATH}/*.espk
			rm -f ${LOADR_PATH}/*.spk
			rm -f ${LOADR_PATH}/stored_version.json
		fi
	fi
elif [ "${FLASH_MODE}" == "ELF" ]; then
	if [ "$#" == "0" ]; then
		echo "ERROR: No elf files are contains."
		echo ""
		show_help
	fi

	# Flash elf files into spresense board
	${SCRIPT_DIR}/${PLATFORM}/xmodem_writer${EXEEXT} -d -c ${UART_PORT} $@
elif [ "${FLASH_MODE}" == "REMOVE" ]; then
	args=()
	for f in "$@"; do
		args+=("-e" "$f")
	done
	# Remove nuttx spk file and other files from spresense board
	${SCRIPT_DIR}/${PLATFORM}/flash_writer${EXEEXT} -s -c ${UART_PORT} -d -n -e nuttx "${args[@]}"
fi
