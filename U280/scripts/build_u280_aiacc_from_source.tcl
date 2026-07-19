# Build U280 AIACC/XDMA/HBM bitstream from source.
#
# Run from the repository root with Vivado 2021.x or 2023.2:
#   vivado -mode batch -source scripts/build_u280_aiacc_from_source.tcl
#
# Optional:
#   vivado -mode batch -source scripts/build_u280_aiacc_from_source.tcl -tclargs C:/path/to/LLAMA_test

set origin_dir [file normalize [pwd]]
if {$argc >= 1} {
    set origin_dir [file normalize [lindex $argv 0]]
}

set fpga_dir  [file join $origin_dir aiacc_fpga]
set aiacc_dir [file join $origin_dir Jianyuan-aiacc_4.21_design_only]
if {![file exists $aiacc_dir]} {
    set aiacc_dir [file join $origin_dir Jianyuan-aiacc_4.21]
}
set build_dir [file join $origin_dir build_u280_aiacc_src]
set proj_name aiacc_u280_src
set vivado_version [version -short]
set vivado_series [join [lrange [split $vivado_version .] 0 1] .]

puts "Vivado version: $vivado_version"
if {![string match "2021.*" $vivado_version] && ![string match "2023.2*" $vivado_version]} {
    puts "WARNING: This flow is intended for Vivado 2021.x or 2023.2; continuing with $vivado_version."
}

proc require_path {path} {
    if {![file exists $path]} {
        error "Required path does not exist: $path"
    }
}

require_path $fpga_dir
require_path $aiacc_dir
require_path [file join $fpga_dir top.v]
require_path [file join $fpga_dir bd design_1.tcl]
require_path [file join $fpga_dir xilinx_au280_board.xdc]

file mkdir $build_dir
create_project -force $proj_name $build_dir -part xcu280-fsvh2892-2L-e
# Some Vivado 2021.2 installations ship an AU280 board file for the ES1 part
# only.  That board part cannot be assigned to the production -2L-e device.
# The repository has a complete board XDC, so board-part metadata is optional.
set au280_board [get_board_parts -quiet xilinx.com:au280:part0:1.1]
if {$au280_board ne ""} {
    set board_device [string tolower [get_property PART_NAME $au280_board]]
    if {$board_device eq "xcu280-fsvh2892-2l-e"} {
        set_property BOARD_PART xilinx.com:au280:part0:1.1 [current_project]
    } else {
        puts "WARNING: Ignoring AU280 board part for $board_device; using production device plus repository XDC."
    }
} else {
    puts "WARNING: AU280 board part is unavailable; using production device plus repository XDC."
}
set_property target_language Verilog [current_project]
set_property default_lib work [current_project]

# Board-level top and constraints.
add_files -norecurse [file join $fpga_dir top.v]
add_files -fileset constrs_1 -norecurse [file join $fpga_dir xilinx_au280_board.xdc]

# Local Xilinx IP (.xci): HBM, DWC, clock wizard, floating point, BRAM.
set xci_files [glob -nocomplain [file join $fpga_dir ip * *.xci]]
if {[llength $xci_files] == 0} {
    error "No XCI files found under $fpga_dir/ip"
}

# Vivado 2021.x cannot parse the Vivado 2023.2 XCI JSON schema.  Changing only
# SWVERSION is unsafe and fails with missing value_src/value_permission fields.
# The IPs must first be recreated/exported by Vivado 2021.2.
if {[string match "2021.*" $vivado_version]} {
    set native_ip_dir [file join $fpga_dir ip_2021_2]
    set native_xci_files [glob -nocomplain [file join $native_ip_dir * * *.xci]]
    if {[llength $native_xci_files] != [llength $xci_files]} {
        puts "Vivado 2021.x cannot read 2023.2 XCI directly; recreating IP from user configuration parameters."
        file delete -force $native_ip_dir
        set recreate_tcl [file join $build_dir recreate_ip_2021_2.tcl]
        exec python3 [file join $origin_dir scripts generate_vivado_ip_recreate_tcl.py] \
            --source-dir [file join $fpga_dir ip] \
            --output-dir $native_ip_dir \
            --output-tcl $recreate_tcl
        source $recreate_tcl
        set native_xci_files [glob -nocomplain [file join $native_ip_dir * * *.xci]]
        if {[llength $native_xci_files] == 0} {
            error "Vivado 2021.2 did not create XCI files below $native_ip_dir"
        }
    }
    set xci_files $native_xci_files
}

read_ip $xci_files
# A newer tool may upgrade old IP. An older tool must not be asked to
# "upgrade" build-local compatibility copies.
if {![string match "2021.*" $vivado_version]} {
    upgrade_ip -quiet [get_ips]
}
generate_target all [get_ips]
report_ip_status -file [file join $build_dir ip_status.rpt]

# Recreate XDMA BD from source Tcl and add its wrapper.
source [file join $fpga_dir bd design_1.tcl]
set bd_file [get_files -quiet *design_1.bd]
if {$bd_file eq ""} {
    error "design_1.bd was not created"
}
make_wrapper -files $bd_file -top
set wrapper_files [glob -nocomplain [file join $build_dir $proj_name.gen sources_1 bd design_1 hdl *wrapper.v]]
if {[llength $wrapper_files] == 0} {
    error "BD wrapper was not generated"
}
add_files -norecurse $wrapper_files

# Add AIACC synthesizable RTL using the repository filelists.  Parsing the
# simple filelists here is more reliable in project mode than read_verilog -f:
# Vivado 2021.2 accepted that command but silently omitted SMC_ALL.sv.
proc add_filelist_from_dir {dir filelist} {
    set fh [open [file join $dir $filelist] r]
    while {[gets $fh line] >= 0} {
        set line [string trim $line]
        if {$line eq "" || [string match "//*" $line] || \
            [string match "#*" $line] || [string match "+incdir+*" $line]} {
            continue
        }
        set src [file normalize [file join $dir $line]]
        if {![file exists $src]} {
            close $fh
            error "Filelist entry does not exist: $src"
        }
        add_files -norecurse $src
        # The release uses SystemVerilog constructs (packed arrays, generate
        # syntax) in files that still carry a .v suffix.
        set_property FILE_TYPE SystemVerilog [get_files $src]
    }
    close $fh
}

add_filelist_from_dir [file join $aiacc_dir smi] smi_rtl.f
add_filelist_from_dir [file join $aiacc_dir smc] smc_rtl_f.f
add_filelist_from_dir [file join $aiacc_dir data_engine] data_engine.f
add_filelist_from_dir [file join $aiacc_dir data_engine sram] sram_rtl.f
add_files -norecurse [file join $aiacc_dir data_engine aiacc_top_f.v]
set_property FILE_TYPE SystemVerilog \
    [get_files [file join $aiacc_dir data_engine aiacc_top_f.v]]

# Include directories needed by `include directives in AIACC RTL.
set_property include_dirs [list \
    [file join $aiacc_dir smi] \
    [file join $aiacc_dir smi DW_axi_gs] \
    [file join $aiacc_dir smc] \
    [file join $aiacc_dir smc common] \
    [file join $aiacc_dir smc define] \
    [file join $aiacc_dir data_engine] \
    [file join $aiacc_dir data_engine noc rtl include] \
    [file join $aiacc_dir data_engine stb_ldb stb] \
] [current_fileset]

set_property top top [current_fileset]
update_compile_order -fileset sources_1
validate_bd_design
save_bd_design

# Build through bitstream.
launch_runs synth_1 -jobs 8
wait_on_run synth_1
open_run synth_1
report_utilization -file [file join $build_dir synth_utilization.rpt]

launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
open_run impl_1
report_timing_summary -file [file join $build_dir timing_summary.rpt]
report_utilization -file [file join $build_dir impl_utilization.rpt]

puts "BITSTREAM: [file join $build_dir $proj_name.runs impl_1 top.bit]"
