`ifndef VX_BRANCH_RSP
`define VX_BRANCH_RSP

`include "../VX_define.vh"

interface VX_branch_response_inter ();

	wire           		valid_branch;
	wire           		branch_dir;
	wire [31:0]     	branch_dest;
	wire [`NW_BITS-1:0] branch_warp_num;

endinterface

`endif