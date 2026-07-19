# Run this on the host with the Corundum PCIe endpoint
run_openocd: local/bin/openocd
	./openocd.sh

# fetch OpenOCD specific for SpinalHDL/VexRiscv
# (as VexRiscv does not use the RISC-V debug specification)
openocd_riscv/tcl/target/vexriscv_sim.cfg:
	git clone https://github.com/SpinalHDL/openocd_riscv && \
	cd openocd_riscv && \
	git checkout 3eee6eb04181d941fadc76ee1cee6d73790250dd

# fetch OpenOCD specific for SpinalHDL/VexRiscv
local/bin/openocd: openocd_riscv/tcl/target/vexriscv_sim.cfg
	cd openocd_riscv && \
	./bootstrap && \
	./configure --prefix=$(PWD)/local --enable-xlnx-pcie-xvc --enable-dummy && \
	make -j16 install

# clean
clean:
	rm -rf openocd_riscv
	rm -rf local
