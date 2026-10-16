#include "unit_argument_test_support.hpp"

int main() {try {
  const auto p=preloads();
  for(bool negative:{false,true}) {
    const auto before=fixture(negative);const auto c=candidates(before,p);
    require(!c.empty(),negative?"negative producer composition missing":"positive producer composition missing");
    const auto after=lower_ir_to_machine(c.front().ops);
    std::cout << "negative=" << negative << " cells=" << cells(before) << "->" << cells(after) << '\n';
    require(cells(before)-cells(after)==(negative?2:1),"exact savings");
    for(const char* payload:{"1","0","8.6","8.Е","8.ЕЕЕЕЕЕЕ","-4"})
      require(observe(before,p,payload)==observe(after,p,payload),std::string("ROM mismatch ")+payload);
  }
  for(const auto& before:{fixture(false,true),fixture(false,false,true),fixture(false,false,false,true),fixture(true,false,false,false,true)})
    require(candidates(before,p).empty(),"unproved X1/Y/nonunit/orientation case accepted");
  auto nonempty=p;nonempty[9].value="1";
  require(candidates(fixture(true),nonempty).empty(),"noncanonical initial word must not obtain empty-set sign fact");
  std::cout << "unit producer/consumer contracts passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
