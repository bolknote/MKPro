#pragma once

#include <memory>
#include <string>
#include <vector>

namespace mkpro::emulator {

struct DisplayCell {
  std::string symbol;
  bool comma = false;
  int digit = 0;
};

struct ProgramLoadResult {
  std::vector<int> codes;
  std::vector<std::string> diagnostics;
};

struct RunResult {
  bool stopped = false;
  int frames = 0;
  std::string signature;
};

struct MK61Options {
  bool extended = true;
  std::string angle_mode = "rad";
};

class MK61 {
 public:
  explicit MK61(MK61Options options = {});
  ~MK61();
  MK61(MK61&&) noexcept;
  MK61& operator=(MK61&&) noexcept;

  MK61(const MK61&) = delete;
  MK61& operator=(const MK61&) = delete;

  MK61& reset();
  MK61& power_on();
  MK61& power_off();
  MK61& frame();
  MK61& frame(int key_x, int key_y);
  MK61& press(const std::string& key);
  MK61& press(int key_x, int key_y);
  MK61& press_sequence(const std::vector<std::string>& keys);
  MK61& input_number(const std::string& value, bool clear = false);
  MK61& run_frames(int count);

  RunResult run_until_stable(int max_frames = 200, int stable_frames = 4);

  std::vector<DisplayCell> display_cells() const;
  std::string display_text(bool raw = false) const;
  std::string program_counter() const;
  int memory_phase() const;
  int command_limit() const;

  ProgramLoadResult load_program(const std::vector<int>& program, bool clear_rest = true);
  std::vector<int> read_program_codes(int count = -1);

  MK61& set_register(const std::string& register_name, const std::string& value);
  std::string read_register(const std::string& register_name);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mkpro::emulator
