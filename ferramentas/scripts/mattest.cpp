#include "ZunMath.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static float rnd(){return (rand()%2000-1000)/100.0f;}
static ZunMatrix R(){ZunMatrix m;for(int i=0;i<4;i++)for(int j=0;j<4;j++)m.m[i][j]=rnd();return m;}
int main(){
  srand(1);float maxerr=0;
  for(int t=0;t<1000;t++){
    ZunMatrix p=R(),view=R(),model=R();
    ZunVec4 v(rnd(),rnd(),rnd(),1.0f);
    ZunMatrix mv=view*model;
    ZunVec4 a=p*(mv*v);          // Software backend
    ZunMatrix mvp=p*(view*model);
    ZunVec4 b=mvp*v;             // PS2GS backend
    float e=fabsf(a.x-b.x)+fabsf(a.y-b.y)+fabsf(a.z-b.z)+fabsf(a.w-b.w);
    float s=fabsf(a.x)+fabsf(a.y)+fabsf(a.z)+fabsf(a.w)+1;
    if(e/s>maxerr)maxerr=e/s;
  }
  printf("max relative error: %g\n",maxerr);
}
