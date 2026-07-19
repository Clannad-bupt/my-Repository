set design TProj
set sim_top tb/tb_pkt_filter.v
set device xcu280-fsvh2892-2L-e
set proj_dir ./project_pktfilter
set public_repo_dir /home/bupt/chen/new-corundum/fpga/mqnic/AU280/fpga_100g/lib_rmt/netfpga_fifo/

create_project -name ${design} -force -dir "${proj_dir}" -part ${device}
set_property source_mgmt_mode DisplayOnly [current_project]
puts "Creating RMT 512b datawidth simulation"

create_fileset -constrset -quiet constrants
set_property ip_repo_paths ${public_repo_dir} [current_fileset]
update_ip_catalog

# dummy
create_ip -name input_arbiter -vendor NetFPGA -library NetFPGA -module_name input_arbiter_ip
set_property generate_synth_checkpoint false [get_files input_arbiter_ip.xci]
reset_target all [get_ips input_arbiter_ip]
generate_target all [get_ips input_arbiter_ip]



source ./tcl/fifo.tcl

update_ip_catalog



# rmt-related
read_verilog "./cookie.v"
read_verilog "./pkt_filter.v"
read_verilog "./fallthrough_small_fifo.v"
read_verilog "./small_fifo.v"
###
read_verilog "./tb/tb_pkt_filter.v"

update_compile_order -fileset sources_1
update_compile_order -fileset sim_1

set_property top ${sim_top} [get_filesets sim_1]
set_property include_dirs ${proj_dir} [get_filesets sim_1]
set_property simulator_language Mixed [current_project]
set_property verilog_define { {SIMULATION=1} } [get_filesets sim_1]
set_property -name xsim.more_options -value {-testplusarg TESTNAME=basic_test} -objects [get_filesets sim_1]
set_property runtime {} [get_filesets sim_1]
set_property target_simulator xsim [current_project]
set_property compxlib.compiled_library_dir {} [current_project]
set_property top_lib xil_defaultlib [get_filesets sim_1]
update_compile_order -fileset sim_1

# workaround to avoid invoking default python2 in vivado
# unset env(PYTHONHOME)
# set output [exec python3 $::env(NF_DESIGN_DIR)/test/${test_name}/run.py]
# puts $output

set_property source_mgmt_mode All [current_project]
update_compile_order -fileset sources_1
set_property -name {xsim.simulate.runtime} -value {10000ns} -objects [get_filesets sim_1]

launch_simulation -simset sim_1 -mode behavioral
run 10us


