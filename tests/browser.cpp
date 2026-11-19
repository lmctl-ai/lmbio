#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <iostream>
extern "C" {
int bio_run(int,double,double,double,double,double,double,double,int,std::uint32_t,int);
const double *bio_output();
int bio_width();
const char *bio_error();
std::size_t bio_limit();
}
int main() {
  assert(bio_limit() == std::size_t(sizeof(std::size_t)==4?256:4096)*1024*1024);
  assert(bio_run(0,1,.5,0,0,0,0,2,21,0,0)==21);
  double coarse=bio_output()[20*3+1], target=std::exp(-1.0);
  for(int i=0;i<21;i++) assert(std::abs(bio_output()[3*i+1]+bio_output()[3*i+2]-1)<1e-9);
  assert(bio_run(0,1,.5,0,0,0,0,2,21,0,1)==21);
  assert(std::abs(bio_output()[61]-target)<1e-9);
  assert(std::abs(bio_output()[61]-target)<std::abs(coarse-target));
  assert(bio_run(1,.01,1,10,5,5,0,2,81,0,1)==81);
  for(int i=0;i<81;i++) {
    const double *p=bio_output()+i*5;
    assert(std::abs(p[1]+p[3]-.01)<1e-9);
    assert(std::abs(p[2]+p[3]+p[4]-1)<1e-9);
  }
  assert(std::abs(bio_output()[404]-.0480600720326477)<1e-8);
  if(bio_run(2,0,6,2,0,0,0,2,21,123,0)!=21) { std::cerr<<bio_error()<<"\n";return 1; }
  double copy[42]; for(int i=0;i<42;i++) copy[i]=bio_output()[i];
  assert(bio_run(2,0,6,2,0,0,0,2,21,123,0)==21);
  for(int i=0;i<42;i++) assert(copy[i]==bio_output()[i]);
  double sum=0; for(unsigned seed=0;seed<256;seed++) {
    assert(bio_run(2,0,6,2,0,0,0,2,2,seed,0)==2);
    double n=bio_output()[3]; assert(n>=0 && n==std::floor(n));sum+=n;
  }
  // 8 standard errors: broad analytic ensemble check, not exact random-path grading.
  double mean=3*(1-std::exp(-4.0));assert(std::abs(sum/256-mean)<8*std::sqrt(mean/256));
  assert(bio_run(0,1,.5,0,0,0,0,21,2,0,0)==-1 && bio_width()==0 && *bio_error());
  assert(bio_run(2,1.5,6,2,0,0,0,2,2,0,0)==-1);
  assert(bio_run(99,0,0,0,0,0,0,1,2,0,0)==-1);
  assert(bio_run(0,1,.5,0,0,0,0,1,258,0,0)==-1);
  std::cout<<"PASS bounded ABI: allocator, conservation, convergence, seeded SSA, 256-seed moment, bounds\n";
}
