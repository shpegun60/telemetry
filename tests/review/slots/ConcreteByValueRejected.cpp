#include "Telemetry.h"
using namespace telemetry;
void f(){ DelegateSlot<void(int&) noexcept> s; s.bind([](int x) noexcept { (void)x; }); }
int main(){}
