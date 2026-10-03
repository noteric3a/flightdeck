#include "standalone.h"
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
  uint16_t pixels[2048];
  render(l,scenario==2||scenario==3?nullptr:&f,pixels,1700021600,scenario==2||scenario==7);
  std::ofstream output(argv[2],std::ios::binary);
  for(uint16_t p:pixels) {output.put(p&255);output.put(p>>8);}
}
