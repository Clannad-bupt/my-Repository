set origin_dir [file normalize [pwd]]
if {$argc >= 1} { set origin_dir [file normalize [lindex $argv 0]] }
set dcp [file join $origin_dir build_u280_aiacc_src aiacc_u280_src.runs synth_1 top.dcp]
if {![file exists $dcp]} { error "Checkpoint not found: $dcp" }
open_checkpoint $dcp
set debug_cores [get_debug_cores -quiet]
puts "DEBUG_CORES_BEFORE: $debug_cores"
foreach core $debug_cores {
    delete_debug_core $core
}
puts "DEBUG_CORES_AFTER: [get_debug_cores -quiet]"
write_checkpoint -force $dcp
close_design
