.ONESHELL:

.PHONY: build
build:
	docker build --build-arg=TERM="linux" --network=host -t vexriscv .


# -e DISPLAY= and -v /tmp/.X11-unix:<...> allows graphical applications inside container to display on host
# --network="host" allows localhost inside container to reach host ports
# --device=/dev/ttyUSB0 assumes BusBlaster v2.5 on host is on /dev/ttyUSB0 (dmesg)
# -v $$PWD/project:<...> means we mount the host project directory for persistent read/write
run:
	docker run -ti --rm \
	--name vexriscv \
	-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix:/tmp/.X11-unix \
	--network="host" \
	--device=/dev/bus \
	-v $$PWD:/home/vexriscv/project -w /home/vexriscv/project \
	vexriscv:latest
#	--device=/dev/ttyUSB0 \


monitor:
	docker exec -ti \
	vexriscv sudo /opt/openocd-riscv/bin/openocd -f interface/ftdi/dp_busblaster.cfg -c "set MURAX_CPU0_YAML vexriscv/cpu0.yaml" -f target/murax.cfg

export:
#	HASH=`docker run --detach vexriscv /bin/true` && docker export $$HASH | tar tv
#	HASH=`docker run --detach vexriscv /bin/true` && docker export $$HASH > vexriscv.tar
	HASH=`docker run --detach vexriscv /bin/true` && docker export $$HASH | xz -T0 > vexriscv.tar.xz

remote:
	# Prepare target env
	export CONTAINER_DISPLAY="0"
	export CONTAINER_HOSTNAME="vexriscv-container"

	# Create a directory for the socket
	rm -rf $${X11TMPDIR}
	export X11TMPDIR=`mktemp -d`
	mkdir -p $${X11TMPDIR}/socket
	touch $${X11TMPDIR}/Xauthority

	# Get the DISPLAY slot
	export DISPLAY_NUMBER=$$(echo $$DISPLAY | cut -d. -f1 | cut -d: -f2)
	echo "DISPLAY_NUMBER=$$DISPLAY_NUMBER"

	# Extract current authentication cookie
	export AUTH_COOKIE=$$(xauth list | grep "^$$(hostname)/unix:$${DISPLAY_NUMBER} " | awk '{print $$3}')
	echo "AUTH_COOKIE=$$AUTH_COOKIE"

	# Create the new X Authority file
	xauth -f $${X11TMPDIR}/Xauthority add $${CONTAINER_HOSTNAME}/unix:$${CONTAINER_DISPLAY} MIT-MAGIC-COOKIE-1 $${AUTH_COOKIE}

	# Proxy with the :0 DISPLAY
	socat UNIX-LISTEN:$${X11TMPDIR}/socket/X$${CONTAINER_DISPLAY},fork TCP4:localhost:60$${DISPLAY_NUMBER} &

	# if user id inside docker container differs from host id
	# we need to provide access for this other user
	# inspired by https://jtreminio.com/blog/running-docker-containers-as-current-host-user/
	chmod ugo+rwx -R $${X11TMPDIR}
	# not sure why this is ALSO needed
	setfacl -R -m user:1000:rwx $${X11TMPDIR}

	# Launch the container
	docker run -it --rm \
	--name vexriscv \
	--hostname $${CONTAINER_HOSTNAME} \
	-u `id -u`:`id -g` \
	-e DISPLAY=:$${CONTAINER_DISPLAY} \
	-e XAUTHORITY=/tmp/.Xauthority \
	-v $${X11TMPDIR}/socket:/tmp/.X11-unix \
	-v $${X11TMPDIR}/Xauthority:/tmp/.Xauthority \
        --device-cgroup-rule 'c 188:* rmw' \
        --device-cgroup-rule 'c 189:* rmw' \
        -v /dev/ttyUSB0:/dev/ttyUSB0:rw \
        -v /dev/ttyUSB1:/dev/ttyUSB1:rw \
        -v /dev/ttyUSB2:/dev/ttyUSB2:rw \
        -v /dev/ttyUSB3:/dev/ttyUSB3:rw \
        -v /dev:/dev:rw \
	-v $$PWD:/home/vexriscv/project -w /home/vexriscv/project \
	vexriscv:latest

	rm -rf $${X11TMPDIR}

#	--device=/dev/bus \

