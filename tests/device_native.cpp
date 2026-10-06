#include "standalone.h"
#include "offline_logo_data.h"
#include <fstream>
#include <vector>
#include <cassert>
#include <cstdlib>
using namespace flightdeck;
int main(int argc,char** argv) {
  assert(argc==4);
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
  Layout l;
  if(!parseLayout(bytes.data(),bytes.size(),l)) return 64;
  // The offline sketch must unpack the same left-to-right pixels and RGB565
  // palette as the shipped layout, including both sides of the Delta widget.
  if (argc == 4 && atoi(argv[3]) == 8) {
    for (unsigned i=0; i<l.logoCount; i++) {
      const auto& logo=l.logos[i];
      if (logo.width!=28 || logo.height!=28) continue;
      const uint64_t* rows=!strcmp(logo.code,"UAL")?LOGO_UAL:!strcmp(logo.code,"DAL")?LOGO_DAL:!strcmp(logo.code,"SWA")?LOGO_SWA:!strcmp(logo.code,"AAL")?LOGO_AAL:nullptr;
      if (!rows) continue;
      for (unsigned y=0; y<28; y++) for (unsigned x=0; x<28; x++)
        assert(logoColor(logo.code,(rows[y]>>(x*2))&3)==read16(logo.pixels+(y*28+x)*3));
    }
  }
  // Sample truncated prefixes must fail without reading outside their bounds.
  for(size_t n=0;n<bytes.size();n+=97) {Layout bad;assert(!parseLayout(bytes.data(),n,bad));}
  auto corrupt=bytes;corrupt[4]=0;Layout bad;assert(!parseLayout(corrupt.data(),corrupt.size(),bad));
  corrupt=bytes;corrupt.push_back(0);assert(!parseLayout(corrupt.data(),corrupt.size(),bad));
  Flight f;
  strcpy(f.id,"UAL-test-id");strcpy(f.callsign,"UAL247");strcpy(f.airline,"UAL");strcpy(f.airlineName,"United Air");
  strcpy(f.origin,"PHL");strcpy(f.destination,"ORD");strcpy(f.aircraft,"B787-9");
  f.altitude=35000;f.speed=430;f.distance=45;f.heading=90;f.verticalRate=-1.5;
  f.departure=1700000000;f.arrival=1700043200;
  int scenario=atoi(argv[3]);
  if(scenario==1) {f=Flight{};strcpy(f.callsign,"N625EC");strcpy(f.airline,"PVT");}
  if(scenario>=4) {f.departure=NAN;f.arrival=NAN;f.progress=scenario==4?0:scenario==5?100:50;}
  if(scenario>=8) strcpy(f.airline,scenario==8?"DAL":scenario==9?"SWA":"AAL");
  uint16_t pixels[2048];
  render(l,scenario==2||scenario==3?nullptr:&f,pixels,1700021600,scenario==2||scenario==7);
  std::ofstream output(argv[2],std::ios::binary);
  for(uint16_t p:pixels) {output.put(p&255);output.put(p>>8);}
}
