#include "simulator.h"
#include <iostream>
#include <iomanip>

Simulator::Simulator(RAM *ram)
    : total_cycles_(0)
    , dram_stalled_(false)
    , I_dram_stalled_(false) {
  ram_ = ram;

#ifdef USE_MULTICORE
  vortex_ = new VVortex_Socket();
#else
  vortex_ = new VVortex();
#endif

#ifdef VCD_OUTPUT
  Verilated::traceEverOn(true);
  trace_ = new VerilatedVcdC;
  vortex_->trace(trace_, 99);
  trace_->open("trace.vcd");
#endif  
}

Simulator::~Simulator() {
#ifdef VCD_OUTPUT
  trace_->close();
#endif
  delete vortex_;
}

void Simulator::print_stats(std::ostream& out) {
  out << std::left;
  out << std::setw(24) << "# of total cycles:" << std::dec << total_cycles_ << std::endl;
}

#ifndef USE_MULTICORE

void Simulator::ibus_driver() {
  // Iterate through each element, and get pop index
  int dequeue_index = -1;
  bool dequeue_valid = false;
  for (int i = 0; i < I_dram_req_vec_.size(); i++) {
    if (I_dram_req_vec_[i].cycles_left > 0) {
      I_dram_req_vec_[i].cycles_left -= 1;
    }

    if ((I_dram_req_vec_[i].cycles_left == 0) && (!dequeue_valid)) {
      dequeue_index = i;
      dequeue_valid = true;
    }
  }

  if (!I_dram_stalled_) {
    // std::cout << "Icache Dram Request received!\n";
    if (vortex_->I_dram_req_read) {
      // std::cout << "Icache Dram Request is read!\n";
      // Need to add an element
      dram_req_t dram_req;
      dram_req.cycles_left = DRAM_LATENCY;
      dram_req.base_addr = vortex_->I_dram_req_addr;
      dram_req.data = (unsigned *)malloc(GLOBAL_BLOCK_SIZE_BYTES);

      for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
        unsigned curr_addr = dram_req.base_addr + (i * 4);
        unsigned data_rd;
        ram_->getWord(curr_addr, &data_rd);
        dram_req.data[i] = data_rd;
      }
      // std::cout << "Fill Req -> Addr: " << std::hex << dram_req.base_addr << std::dec << "\n";
      I_dram_req_vec_.push_back(dram_req);
    }

    if (vortex_->I_dram_req_write) {
      unsigned base_addr = vortex_->I_dram_req_addr;

      for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
        unsigned curr_addr = base_addr + (i * 4);
        unsigned data_wr = vortex_->I_dram_req_data[i];
        ram_->writeWord(curr_addr, &data_wr);
      }
    }
  }

  if (vortex_->I_dram_rsp_ready && dequeue_valid) {
    // std::cout << "Icache Dram Response Sending...!\n";

    vortex_->I_dram_rsp_valid = 1;
    vortex_->I_dram_rsp_addr = I_dram_req_vec_[dequeue_index].base_addr;
    // std::cout << "Fill Rsp -> Addr: " << std::hex << (I_dram_req_vec_[dequeue_index].base_addr) << std::dec << "\n";

    for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
      vortex_->I_dram_rsp_data[i] = I_dram_req_vec_[dequeue_index].data[i];
    }
    free(I_dram_req_vec_[dequeue_index].data);

    I_dram_req_vec_.erase(I_dram_req_vec_.begin() + dequeue_index);
  } else {
    vortex_->I_dram_rsp_valid = 0;
    vortex_->I_dram_rsp_addr = 0;
  }

#ifdef ENABLE_DRAM_STALLS
  I_dram_stalled_ = false;
  if (0 == (total_cycles_ % DRAM_STALLS_MODULO)) { 
    I_dram_stalled_ = true;
  } else
  if (I_dram_req_vec_.size() >= DRAM_RQ_SIZE) {
    I_dram_stalled_ = true;
  }  
#endif

  vortex_->dram_req_full = I_dram_stalled_;
}

#endif

void Simulator::dbus_driver() {
  // Iterate through each element, and get pop index
  int dequeue_index = -1;
  bool dequeue_valid = false;
  for (int i = 0; i < dram_req_vec_.size(); i++) {
    if (dram_req_vec_[i].cycles_left > 0) {
      dram_req_vec_[i].cycles_left -= 1;
    }

    if ((dram_req_vec_[i].cycles_left == 0) && (!dequeue_valid)) {
      dequeue_index = i;
      dequeue_valid = true;
    }
  }

#ifdef ENABLE_DRAM_STALLS
  dram_stalled_ = false;
  if (0 == (total_cycles_ % DRAM_STALLS_MODULO)) { 
    dram_stalled_ = true;
  } else
  if (dram_req_vec_.size() >= DRAM_RQ_SIZE) {
    dram_stalled_ = true;
  }  
#endif

#ifdef USE_MULTICORE

  if (!dram_stalled_) {
    if (vortex_->dram_req_read) {
      // Need to add an element
      dram_req_t dram_req;
      dram_req.cycles_left = DRAM_LATENCY;
      dram_req.base_addr = vortex_->dram_req_addr;
      dram_req.data = (unsigned *)malloc(GLOBAL_BLOCK_SIZE_BYTES);

      for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
        unsigned curr_addr = dram_req.base_addr + (i * 4);
        unsigned data_rd;
        ram_->getWord(curr_addr, &data_rd);
        dram_req.data[i] = data_rd;
      }
      dram_req_vec_.push_back(dram_req);
    }

    if (vortex_->dram_req_write) {
      unsigned base_addr = vortex_->dram_req_addr;

      for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
        unsigned curr_addr = base_addr + (i * 4);
        unsigned data_wr = vortex_->dram_req_data[i];
        ram_->writeWord(curr_addr, &data_wr);
      }
    }
  }

  if (vortex_->dram_rsp_ready && dequeue_valid) {
    vortex_->dram_rsp_valid = 1;
    vortex_->dram_rsp_addr = dram_req_vec_[dequeue_index].base_addr;

    for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
      vortex_->dram_rsp_data[i] = dram_req_vec_[dequeue_index].data[i];
    }
    free(dram_req_vec_[dequeue_index].data);

    dram_req_vec_.erase(dram_req_vec_.begin() + dequeue_index);
  } else {
    vortex_->dram_rsp_valid = 0;
    vortex_->dram_rsp_addr = 0;
  }

#else

  if (!dram_stalled_) {
    if (vortex_->dram_req_read) {
      // Need to add an element
      dram_req_t dram_req;
      dram_req.cycles_left = DRAM_LATENCY;
      dram_req.base_addr = vortex_->dram_req_addr;
      dram_req.data = (unsigned *)malloc(GLOBAL_BLOCK_SIZE_BYTES);

      for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
        unsigned curr_addr = dram_req.base_addr + (i * 4);
        unsigned data_rd;
        ram_->getWord(curr_addr, &data_rd);
        dram_req.data[i] = data_rd;
      }
      dram_req_vec_.push_back(dram_req);
    }

    if (vortex_->dram_req_write) {
      unsigned base_addr = vortex_->dram_req_addr;

      for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
        unsigned curr_addr = base_addr + (i * 4);
        unsigned data_wr = vortex_->dram_req_data[i];
        ram_->writeWord(curr_addr, &data_wr);
      }
    }
  }

  if (vortex_->dram_rsp_ready && dequeue_valid) {
    vortex_->dram_rsp_valid = 1;
    vortex_->dram_rsp_addr = dram_req_vec_[dequeue_index].base_addr;

    for (int i = 0; i < (GLOBAL_BLOCK_SIZE_BYTES / 4); i++) {
      vortex_->dram_rsp_data[i] = dram_req_vec_[dequeue_index].data[i];
    }
    free(dram_req_vec_[dequeue_index].data);

    dram_req_vec_.erase(dram_req_vec_.begin() + dequeue_index);
  } else {
    vortex_->dram_rsp_valid = 0;
    vortex_->dram_rsp_addr = 0;
  }
  
#endif

#ifdef USE_MULTICORE
  vortex_->dram_req_full = dram_stalled_;
#else
  vortex_->dram_req_full = dram_stalled_;
#endif
}

void Simulator::io_handler() {
#ifdef USE_MULTICORE
  bool io_valid = false;
  for (int c = 0; c < NUM_CORES; c++) {
    if (vortex_->io_valid[c]) {
      uint32_t data_write = (uint32_t)vortex_->io_data[c];
      char c = (char)data_write;
      std::cerr << c;      
      io_valid = true;
    }
  }
  if (io_valid) {
    std::cout << std::flush;
  }
#else
  if (vortex_->io_valid) {
    uint32_t data_write = (uint32_t)vortex_->io_data;
    char c = (char)data_write;
    std::cerr << c;
    std::cout << std::flush;
  }
#endif
}

void Simulator::reset() {  
  vortex_->reset = 1;
  this->step();
  vortex_->reset = 0;
}

void Simulator::step() {
  vortex_->clk = 0;
  vortex_->eval();

#ifdef VCD_OUTPUT
  trace_->dump(2 * total_cycles_ + 0);
#endif

  vortex_->clk = 1;
  vortex_->eval();

#ifndef USE_MULTICORE
  ibus_driver();
#endif

  dbus_driver();
  io_handler();

#ifdef VCD_OUTPUT
  trace_->dump(2 * total_cycles_ + 1);
#endif

  ++total_cycles_;
}

void Simulator::wait(uint32_t cycles) {
  for (int i = 0; i < cycles; ++i) {
    this->step();
  }
}

bool Simulator::is_busy() {
  return (0 == vortex_->out_ebreak);
}

void Simulator::send_snoops(uint32_t mem_addr, uint32_t size) {
  // align address to LLC block boundaries
  auto aligned_addr_start = GLOBAL_BLOCK_SIZE_BYTES * (mem_addr / GLOBAL_BLOCK_SIZE_BYTES);
  auto aligned_addr_end = GLOBAL_BLOCK_SIZE_BYTES * ((mem_addr + size + GLOBAL_BLOCK_SIZE_BYTES - 1) / GLOBAL_BLOCK_SIZE_BYTES);

#ifdef USE_MULTICORE
  // submit snoop requests for the needed blocks
  vortex_->llc_snp_req_addr = aligned_addr_start;
  vortex_->llc_snp_req_valid = false;
  for (;;) {
    this->step();
    if (vortex_->llc_snp_req_valid) {
      vortex_->llc_snp_req_valid = false;
      if (vortex_->llc_snp_req_addr >= aligned_addr_end)
        break;
      vortex_->llc_snp_req_addr += GLOBAL_BLOCK_SIZE_BYTES;
    }    
    if (!vortex_->llc_snp_req_full) {
      vortex_->llc_snp_req_valid = true;      
    }
  }
#else
  // submit snoop requests for the needed blocks
  vortex_->snp_req_addr = aligned_addr_start;
  vortex_->snp_req_valid = false;
  for (;;) {
    this->step();
    if (vortex_->snp_req_valid) {
      vortex_->snp_req_valid = false;
      if (vortex_->snp_req_addr >= aligned_addr_end)
        break;
      vortex_->snp_req_addr += GLOBAL_BLOCK_SIZE_BYTES;
    }    
    if (!vortex_->snp_req_full) {
      vortex_->snp_req_valid = true;      
    }
  }
#endif
}

void Simulator::flush_caches(uint32_t mem_addr, uint32_t size) {
  printf("[sim] total cycles: %ld\n", this->total_cycles_);

  // send snoops for L1 flush
  this->send_snoops(mem_addr, size);
  this->wait(PIPELINE_FLUSH_LATENCY);

// #if NUM_CORES != 1
  // send snoops for L2 flush
  // this->send_snoops(mem_addr, size);
  // this->wait(PIPELINE_FLUSH_LATENCY);
// #endif
}

bool Simulator::run() {
  // reset the device
  this->reset();

  // execute program
  while (!vortex_->out_ebreak) {
    this->step();
  }

  // wait 5 cycles to flush the pipeline
  this->wait(5);

#ifdef USE_MULTICORE
  int status = 0;
#else
  // check riscv-tests PASSED/FAILED status
  int status = (int)vortex_->Vortex->vx_back_end->vx_wb->last_data_wb & 0xf;
#endif

  return (status == 1);
}
