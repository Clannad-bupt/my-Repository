set origin_dir [file normalize [pwd]]
if {$argc >= 1} {
    set origin_dir [file normalize [lindex $argv 0]]
}
set build_dir [file join $origin_dir build_u280_aiacc_src]
set project [file join $build_dir aiacc_u280_src.xpr]
if {![file exists $project]} {
    error "Project not found: $project"
}
open_project $project
if {[get_runs -quiet synth_1] eq "" || [get_property STATUS [get_runs synth_1]] ne "synth_design Complete!"} {
    error "synth_1 is not complete"
}
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "impl_1 failed: [get_property STATUS [get_runs impl_1]]"
}
puts "BITSTREAM: [file join $build_dir aiacc_u280_src.runs impl_1 top.bit]"
