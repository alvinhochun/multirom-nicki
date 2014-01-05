#!/multirom/busybox sh
set -e
set -x
alias bb=/multirom/busybox
exec > /multirom/setup.log 2>&1
fail () {
	trap - EXIT
	trap - 1
	echo "01 0 1" > /sys/class/led/fih_led/control
	echo "MultiROM setup failed, here is the log:" > /dev/kmsg
	bb sed 's/^/setup: /' /multirom/setup.log > /dev/kmsg
	echo "1" > "/sys/devices/platform/msm_sdcc.1/mmc_host/mmc0/mmc0:0001/block/mmcblk0/force_ro"
	/multirom/reboot recovery
}
trap fail EXIT
trap fail 1
PID=$$
{
	bb sleep 7
	kill -1 $PID
} &
PC=$!
waitfor () {
	while [ ! -e "$1" ]
	do
		bb sleep .1
	done
}
replaceblk () {
	[ -f "$2" ] || return 1
	L=$(bb losetup -f)
	waitfor "$L"
	bb losetup "$L" "$2"
	R=$(bb readlink -fn "$1")
	waitfor "$R"
	bb rm -f "$R"
	bb ln -s "$L" "$R"
}

. /multirom/setup-gen.sh

kill $PC
echo "MultiROM setup log:" > /dev/kmsg
bb sed 's/^/setup: /' /multirom/setup.log > /dev/kmsg
trap - EXIT
bb touch /multirom/ready

